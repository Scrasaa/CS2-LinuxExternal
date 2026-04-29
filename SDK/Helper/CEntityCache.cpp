// ── CEntityCache ─────────────────────────────────────────────
//
//  Speed improvements over original (no struct-batching / hot blocks):
//
//  1. Chunk-ptr cache (m_chunk_ptrs[])
//     Pass 1 already reads all bucket ptrs in one shot. That array is now
//     stored as a member so resolve_entity_from_handle() reuses it for the
//     rest of the frame — zero extra RPM per handle lookup instead of 1.
//
//  2. get_pawns() / get_player_pairs() — 0 RPM
//     Pawns are already resolved in Pass 2 and stored in m_cached_players.
//     Both functions now iterate that vector directly instead of re-resolving
//     handles via RPM.
//
//  3. Weapon vtable cache (m_weapon_vtable_set)
//     is_class() costs 4 RPM (vtable → RTTI → name ptr → ReadString).
//     After the first hit we store the vtable address in an unordered_set.
//     Subsequent frames cost 1 RPM (ReadMem vtable) + O(1) hash lookup.
//     The set persists across frames; vtable addresses are stable for the
//     lifetime of the game module.
//
//  4. resolve_entity_from_handle / resolve_weapon_from_handle
//     Both use m_chunk_ptrs[] — saves 1 RPM per call vs the original which
//     always re-read the bucket pointer from the remote process.
//
// ─────────────────────────────────────────────────────────────

#include "CEntityCache.h"
#include "CSchemaManager.h"
#include "Utils/Utils.h"
#include "../weapon_type.h"

static constexpr uint32_t k_max_entities    = 256; // 1024 is too much, right?
static constexpr uint32_t k_slots_per_chunk = 128;
static constexpr uint32_t k_num_chunks      = (k_max_entities + k_slots_per_chunk - 1u) / k_slots_per_chunk;
static constexpr size_t   k_chunk_bytes     = k_entity_identity_size * k_slots_per_chunk;

// ── Construction ──────────────────────────────────────────────

CEntityCache::CEntityCache(uintptr_t a_p_entity_list)
    : m_p_entity_list(a_p_entity_list)
{
    m_players.reserve(64);
    m_weapons.reserve(64);
    m_cached_players.reserve(20);
    std::memset(m_chunk_ptrs, 0, sizeof(m_chunk_ptrs));
}

uintptr_t CEntityCache::read_list_head() const
{
    return R().ReadMem<uintptr_t>(m_p_entity_list + k_entity_list_head_offset);
}

// ── Handle resolution (uses cached chunk ptrs — 1 RPM, not 2) ─

uintptr_t CEntityCache::resolve_entity_from_handle(uint32_t a_handle) const
{
    if (!a_handle)
        return 0;

    const uint32_t l_index  = a_handle & 0x7FFF;
    const uint32_t l_bucket = l_index >> 9;
    const uint32_t l_offset = l_index & 0x1FF;

    if (l_bucket >= k_num_chunks || !is_valid_ptr(m_chunk_ptrs[l_bucket]))
        return 0;

    return R().ReadMem<uintptr_t>(m_chunk_ptrs[l_bucket] + k_entity_identity_size * l_offset);
}

uintptr_t CEntityCache::resolve_weapon_from_handle(uint32_t a_handle) const
{
    if (!a_handle)
        return 0;

    const uint32_t l_index  = a_handle & 0xFFF;
    const uint32_t l_bucket = l_index >> 9;
    const uint32_t l_offset = l_index & 0x1FF;

    if (l_bucket >= k_num_chunks || !is_valid_ptr(m_chunk_ptrs[l_bucket]))
        return 0;

    const uintptr_t lp_entity = R().ReadMem<uintptr_t>(
        m_chunk_ptrs[l_bucket] + k_entity_identity_size * l_offset);

    return is_valid_ptr(lp_entity) ? lp_entity : 0;
}

uintptr_t CEntityCache::read_entity_at_index(uint32_t a_index) const
{
    const uint32_t l_bucket = a_index >> 9;
    const uint32_t l_offset = a_index & 0x1FF;

    if (l_bucket >= k_num_chunks || !is_valid_ptr(m_chunk_ptrs[l_bucket]))
        return 0;

    const uintptr_t lp_entity = R().ReadMem<uintptr_t>(
        m_chunk_ptrs[l_bucket] + k_entity_identity_size * l_offset);

    return is_valid_ptr(lp_entity) ? lp_entity : 0;
}

// ── Weapon vtable cache ───────────────────────────────────────
//
//  Frame 0 (cold): pays 4 RPM for the RTTI walk, inserts vtable into set.
//  Frame 1+ (warm): pays 1 RPM (ReadMem vtable) + hash lookup. Done.

bool CEntityCache::is_weapon_entity(uintptr_t a_p_instance) const
{
    if (!is_valid_ptr(a_p_instance))
        return false;

    const uintptr_t lp_vtable = R().ReadMem<uintptr_t>(a_p_instance);
    if (!is_valid_ptr(lp_vtable))
        return false;

    if (m_weapon_vtable_set.count(lp_vtable))
        return true;

    // Cache miss — pay the full RTTI walk once then never again.
    const std::string l_name = get_classname(a_p_instance);
    if (l_name == "CWeaponBase" || l_name == "CC4")
    {
        m_weapon_vtable_set.insert(lp_vtable);
        return true;
    }

    return false;
}

// ── PlayerInfo population (logic unchanged, 1 RPM saved on weapon resolve) ─

static void populate_player_info(
    uintptr_t   lp_controller,
    uintptr_t   lp_pawn,
    PlayerInfo& out_info)
{
    out_info.szName     = R().ReadString(lp_controller + SCHEMA_OFFSET(CBasePlayerController, m_iszPlayerName));
    out_info.iTeamNum   = R().ReadMem<int32_t>(lp_pawn + SCHEMA_OFFSET(C_BaseEntity, m_iTeamNum));
    out_info.bLifeState = R().ReadMem<uint8_t>(lp_pawn + SCHEMA_OFFSET(C_BaseEntity, m_lifeState));

    out_info.iHealth    = R().ReadMem<int32_t>(lp_pawn + SCHEMA_OFFSET(C_BaseEntity, m_iHealth));
    out_info.iMaxHealth = R().ReadMem<int32_t>(lp_pawn + SCHEMA_OFFSET(C_BaseEntity, m_iMaxHealth));
    out_info.iArmor     = R().ReadMem<int32_t>(lp_pawn + SCHEMA_OFFSET(C_CSPlayerPawn, m_ArmorValue));

    const auto weapon_services = R().ReadMem<uintptr_t>(lp_pawn + SCHEMA_OFFSET(C_BasePlayerPawn, m_pWeaponServices));
    const auto weapon_handle   = R().ReadMem<uintptr_t>(weapon_services + SCHEMA_OFFSET(CPlayer_WeaponServices, m_hActiveWeapon));
    const auto l_wep_index     = static_cast<uint32_t>(reinterpret_cast<uint64_t>(weapon_handle) & 0xFFF);

    // Uses cached chunk ptrs — saves 1 RPM vs original resolve_entity_from_handle.
    const auto weapon_ent = g_EntityCache.resolve_weapon_from_handle(l_wep_index);

    out_info.iReserveAmmo = R().ReadMem<uintptr_t>(weapon_ent + SCHEMA_OFFSET(C_BasePlayerWeapon, m_pReserveAmmo));
    out_info.iClipPrimary = R().ReadMem<uintptr_t>(weapon_ent + SCHEMA_OFFSET(C_BasePlayerWeapon, m_iClip1));

    const auto wep_def = R().ReadMem<uint16_t>(weapon_ent + SCHEMA_OFFSET(C_EconEntity, m_AttributeManager) +
        SCHEMA_OFFSET(C_AttributeContainer, m_Item) + SCHEMA_OFFSET(C_EconItemView, m_iItemDefinitionIndex));

    const auto weapon_type      = weapon_type_from_index(wep_def);
    out_info.szActiveWeaponName = weapon_display_name(weapon_type);
    out_info.iWeaponDef         = wep_def;

    out_info.flags = 0;

    if (R().ReadMem<int8_t>(lp_pawn + SCHEMA_OFFSET(C_CSPlayerPawn, m_bIsScoped)))
        out_info.SetFlag(FLAG_SCOPED);

    if (R().ReadMem<int8_t>(lp_pawn + SCHEMA_OFFSET(C_CSPlayerPawn, m_bIsDefusing)))
        out_info.SetFlag(FLAG_DEFUSING);

    if (R().ReadMem<float>(lp_pawn + SCHEMA_OFFSET(C_CSPlayerPawnBase, m_flFlashDuration)) > 0.1f)
        out_info.SetFlag(FLAG_FLASHED);

    const auto lp_item_services = R().ReadMem<uintptr_t>(
        lp_pawn + SCHEMA_OFFSET(C_BasePlayerPawn, m_pItemServices));

    if (is_valid_ptr(lp_item_services))
    {
        if (R().ReadMem<int8_t>(lp_item_services + SCHEMA_OFFSET(CCSPlayer_ItemServices, m_bHasDefuser)))
            out_info.SetFlag(FLAG_HAS_DEFUSER);

        if (R().ReadMem<int8_t>(lp_item_services + SCHEMA_OFFSET(CCSPlayer_ItemServices, m_bHasHelmet)))
            out_info.SetFlag(FLAG_HAS_HELMET);
    }
}

// ── Refresh ───────────────────────────────────────────────────

bool CEntityCache::refresh()
{
    m_local_controller = R().ReadMem<uintptr_t>(m_localplayer_ptr);
    if (!is_valid_ptr(m_local_controller))
        return false;

    // Populate chunk-ptr cache first — everything else depends on it.
    if (!R().ReadRaw(m_p_entity_list, m_chunk_ptrs, sizeof(uintptr_t) * k_num_chunks))
        return false;

    m_local_pawn = resolve_entity_from_handle(
        R().ReadMem<uint32_t>(m_local_controller + SCHEMA_OFFSET(CBasePlayerController, m_hPawn)));

    if (!is_valid_ptr(m_local_pawn))
        return false;

    m_local_team = R().ReadMem<int32_t>(m_local_pawn + SCHEMA_OFFSET(C_BaseEntity, m_iTeamNum));

    m_players.clear();
    m_weapons.clear();
    m_cached_players.clear();

    // ── Pass 1: bulk chunk reads → raw entity lists ───────────

    alignas(8) uint8_t l_chunk_buf[k_chunk_bytes];

    for (uint32_t l_chunk_idx = 0; l_chunk_idx < k_num_chunks; ++l_chunk_idx)
    {
        const uintptr_t lp_chunk = m_chunk_ptrs[l_chunk_idx];
        if (!is_valid_ptr(lp_chunk))
            continue;

        if (!R().ReadRaw(lp_chunk, l_chunk_buf, k_chunk_bytes))
            continue;

        const uint32_t l_base_index = l_chunk_idx * k_slots_per_chunk;

        for (uint32_t l_slot = 0; l_slot < k_slots_per_chunk; ++l_slot)
        {
            const uint32_t l_entity_index = l_base_index + l_slot;

            if (l_entity_index == 0 || l_entity_index > k_max_entities)
                continue;

            const uint8_t* lp_identity = l_chunk_buf + k_entity_identity_size * l_slot;

            uint32_t l_handle = 0;
            std::memcpy(&l_handle, lp_identity + k_identity_handle, sizeof(l_handle));

            if ((l_handle & 0x7FFF) != l_entity_index)
                continue;

            uintptr_t lp_instance = 0;
            std::memcpy(&lp_instance, lp_identity, sizeof(lp_instance));

            if (!is_valid_ptr(lp_instance) || lp_instance == m_local_controller)
                continue;

            if (l_entity_index <= 64)
            {
                m_players.push_back(lp_instance);
            }
            else
            {
                // Warm path (frame 1+): 1 RPM + hash lookup.
                // Cold path (frame 0):  4 RPM RTTI walk, then cached forever.
                if (is_weapon_entity(lp_instance))
                    m_weapons.push_back(lp_instance);
            }
        }
    }

    // ── Pass 2: per-controller → populate m_cached_players ───

    for (const uintptr_t lp_controller : m_players)
    {
        const auto l_pawn_handle = R().ReadMem<uint32_t>(
            lp_controller + SCHEMA_OFFSET(CBasePlayerController, m_hPawn));

        // 1 RPM (entity ptr only) — bucket ptr comes from m_chunk_ptrs[].
        const uintptr_t lp_pawn = resolve_entity_from_handle(l_pawn_handle);
        if (!is_valid_ptr(lp_pawn))
            continue;

        if (R().ReadMem<uint8_t>(lp_pawn + SCHEMA_OFFSET(C_BaseEntity, m_lifeState)) != 0)
            continue;

        const uintptr_t lp_game_scene = R().ReadMem<uintptr_t>(
            lp_pawn + SCHEMA_OFFSET(C_BaseEntity, m_pGameSceneNode));

        if (!is_valid_ptr(lp_game_scene))
            continue;

        if (R().ReadMem<bool>(lp_game_scene + SCHEMA_OFFSET(CGameSceneNode, m_bDormant)))
            continue;

        CachedPlayer& cp = m_cached_players.emplace_back();
        cp.p_controller  = lp_controller;
        cp.p_pawn        = lp_pawn;
        cp.p_game_scene  = lp_game_scene;

        populate_player_info(lp_controller, lp_pawn, cp.info);
    }

    return !m_players.empty();
}

// ── Accessors ─────────────────────────────────────────────────

const std::vector<CEntityCache::CachedPlayer>& CEntityCache::get_cached_players() const
{
    return m_cached_players;
}

const std::vector<uintptr_t>& CEntityCache::get_controllers() const { return m_players; }
const std::vector<uintptr_t>& CEntityCache::get_players()     const { return m_players; }
const std::vector<uintptr_t>& CEntityCache::get_weapons()     const { return m_weapons; }

int32_t CEntityCache::get_player_count() const
{
    return static_cast<int32_t>(m_players.size());
}

// 0 RPM — pawns already resolved and stored in m_cached_players during Pass 2.

std::vector<uintptr_t> CEntityCache::get_pawns() const
{
    std::vector<uintptr_t> l_pawns;
    l_pawns.reserve(m_cached_players.size());

    for (const CachedPlayer& l_cp : m_cached_players)
        l_pawns.push_back(l_cp.p_pawn);

    return l_pawns;
}

std::vector<CEntityCache::EntityPair> CEntityCache::get_player_pairs() const
{
    std::vector<EntityPair> l_pairs;
    l_pairs.reserve(m_cached_players.size());

    for (const CachedPlayer& l_cp : m_cached_players)
        l_pairs.push_back({ l_cp.p_controller, l_cp.p_pawn });

    return l_pairs;
}

// ── Linked-list walk ──────────────────────────────────────────

CEntityCache::IterResult CEntityCache::iterate(
    const std::function<bool(int32_t, uintptr_t)>& a_fn) const
{
    if (!is_valid_ptr(m_p_entity_list))
        return IterResult::InvalidList;

    uintptr_t lp_identity = read_list_head();
    int32_t   l_index     = 0;

    while (is_valid_ptr(lp_identity))
    {
        const auto l_handle = R().ReadMem<uint32_t>(lp_identity + k_identity_handle);

        if ((l_handle & 0xFFFF) != 0xFFFF)
        {
            const auto lp_instance = R().ReadMem<uintptr_t>(lp_identity);
            if (is_valid_ptr(lp_instance))
            {
                if (!a_fn(l_index++, lp_instance))
                    return IterResult::Aborted;
            }
        }

        lp_identity = R().ReadMem<uintptr_t>(lp_identity + k_identity_next);
    }

    return IterResult::Completed;
}

// ── Class-name helpers ────────────────────────────────────────

std::string CEntityCache::get_classname(uintptr_t a_p_entity_instance) const
{
    if (!is_valid_ptr(a_p_entity_instance))
        return {};

    const auto lp_vtable = R().ReadMem<uintptr_t>(a_p_entity_instance);
    if (!is_valid_ptr(lp_vtable))
        return {};

    const auto lp_rtti = R().ReadMem<uintptr_t>(lp_vtable - 0x8);
    if (!is_valid_ptr(lp_rtti))
        return {};

    const auto lp_name = R().ReadMem<uintptr_t>(lp_rtti + 0x8);
    if (!is_valid_str_ptr(lp_name))
        return {};

    std::string l_name = R().ReadString(lp_name);

    const auto l_start = l_name.find_first_not_of("0123456789");
    if (l_start != std::string::npos)
        l_name = l_name.substr(l_start);

    return l_name;
}

bool CEntityCache::is_class(uintptr_t a_p_identity, const char* a_name) const
{
    const std::string l_name = get_classname(a_p_identity);
    return !l_name.empty() && l_name == a_name;
}

// ── Index resolution ──────────────────────────────────────────

uintptr_t CEntityCache::get_controller_at_index(uint32_t a_index) const
{
    if (!is_valid_ptr(m_p_entity_list))
        return 0;

    const uint32_t l_bucket = a_index >> 9;
    const uint32_t l_offset = a_index & 0x1FF;

    if (l_bucket >= k_num_chunks || !is_valid_ptr(m_chunk_ptrs[l_bucket]))
        return 0;

    const uintptr_t lp_identity = m_chunk_ptrs[l_bucket] + k_entity_identity_size * l_offset;

    const auto l_handle = R().ReadMem<uint32_t>(lp_identity + k_identity_handle);
    if ((l_handle & 0x7FFF) != a_index)
        return 0;

    const auto lp_controller = R().ReadMem<uintptr_t>(lp_identity);
    if (!is_valid_ptr(lp_controller))
        return 0;

    if (!is_class(lp_controller, "CCSPlayerController"))
        return 0;

    return lp_controller;
}