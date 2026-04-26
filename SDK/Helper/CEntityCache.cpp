// ── CEntityCache ─────────────────────────────────────────────
//
//  Entity list layout (CS2 CEntitySystem / IEntityList):
//
//    m_p_entity_list ──► [ ptr to chunk 0 | ptr to chunk 1 | … ]   (8 bytes each)
//                               │
//                               ▼
//                        CEntityIdentity[0..511]   (k_entity_identity_size bytes each)
//                               │
//                        [0x00] m_pInstance  ←  what we store / return
//
//  For a given entity index i:
//      bucket_index    = i >> 9          (which chunk pointer to load)
//      index_in_bucket = i & 0x1FF       (slot inside that chunk)
//
//  Controller → Pawn resolution:
//      CCSPlayerController + 0x7C0 = CHandle<CCSPlayerPawn>
//      handle >> 9        = bucket_index
//      handle & 0x1FF     = index_in_bucket
//      Then same two-step chunk read as refresh().
//
// ─────────────────────────────────────────────────────────────

#include "CEntityCache.h"

#include "CSchemaManager.h"
#include "Utils/Utils.h"

#define MAX_ENTITY 1024

CEntityCache::CEntityCache(uintptr_t a_p_entity_list)
    : m_p_entity_list(a_p_entity_list)
{
    m_players.reserve(64);
    m_weapons.reserve(64);
}

uintptr_t CEntityCache::read_list_head() const
{
    return R().ReadMem<uintptr_t>(m_p_entity_list + k_entity_list_head_offset);
}

// ── Internal helpers ──────────────────────────────────────────

uintptr_t CEntityCache::resolve_entity_from_handle(uint32_t handle) const
{
    if (!handle)
        return 0;

    const uint32_t index  = handle & 0x7FFF;   // mask serial bits
    const uint32_t bucket = index >> 9;
    const uint32_t offset = index & 0x1FF;

    const auto chunk = R().ReadMem<uintptr_t>(
        m_p_entity_list + sizeof(uintptr_t) * bucket);

    if (!chunk)
        return 0;

    const auto entity = R().ReadMem<uintptr_t>(
        chunk + k_entity_identity_size * offset);

    return entity;
}

uintptr_t CEntityCache::read_entity_at_index(uint32_t a_index) const
{
    const uint32_t  l_bucket_index    = a_index >> 9;
    const uint32_t  l_index_in_bucket = a_index & 0x1FF;

    const auto lp_chunk = R().ReadMem<uintptr_t>(
        m_p_entity_list + sizeof(uintptr_t) * l_bucket_index);

    if (!is_valid_ptr(lp_chunk))
        return 0;

    const auto lp_entity = R().ReadMem<uintptr_t>(
        lp_chunk + k_entity_identity_size * l_index_in_bucket);

    return is_valid_ptr(lp_entity) ? lp_entity : 0;
}

// ── Refresh ───────────────────────────────────────────────────
// ── CEntityCache::refresh ─────────────────────────────────────
//
//  Optimised for large entity ranges (up to MAX_ENTITY = 1024).
//
//  Old approach : N × (chunk_ptr + handle + instance + is_class)
//                 = ~7 000 RPM calls at 1024 slots
//
//  New approach : ceil(N/512) bulk chunk reads parsed locally,
//                 is_class only on valid candidates
//                 = ~2 RPM calls + 1 per live entity
//
// ─────────────────────────────────────────────────────────────

bool CEntityCache::refresh()
{
    if (!is_valid_ptr(m_p_entity_list))
        return false;

    m_local_controller = R().ReadMem<uintptr_t>(m_localplayer_ptr);
    if (!is_valid_ptr(m_local_controller))
        return false;

    m_local_pawn = resolve_entity_from_handle(
        R().ReadMem<uint32_t>(
            m_local_controller + SCHEMA_OFFSET(CBasePlayerController, m_hPawn)));
    if (!is_valid_ptr(m_local_pawn))
        return false;

    m_players.clear();
    m_weapons.clear();

    // ── 1. Read all bucket (chunk) pointers in one shot ──────
    //
    //  1024 slots → 2 buckets (each covers 512 identities).
    //  Read the entire pointer array at once instead of one ptr per loop.

    constexpr uint32_t k_max_entities  = MAX_ENTITY;
    constexpr uint32_t k_slots_per_chunk = 512u;
    constexpr uint32_t k_num_chunks =
        (k_max_entities + k_slots_per_chunk - 1u) / k_slots_per_chunk;

    uintptr_t l_chunk_ptrs[k_num_chunks] = {};
    if (!R().ReadRaw(m_p_entity_list,
                        l_chunk_ptrs,
                        sizeof(uintptr_t) * k_num_chunks))
        return false;

    // ── 2. Per-chunk: read the entire identity array locally ──
    //
    //  k_entity_identity_size × 512 bytes per chunk.
    //  Parsing handle + instance is now just a memcpy — zero RPM.

    constexpr size_t k_chunk_bytes = k_entity_identity_size * k_slots_per_chunk;

    // Reuse one stack buffer per chunk to avoid heap alloc in the hot path.
    // Stack is fine: ~60 KB for typical k_entity_identity_size of 0x78.
    // If your identity struct is larger, promote to a member buffer.
    alignas(8) uint8_t l_chunk_buf[k_chunk_bytes];

    for (uint32_t l_chunk_idx = 0; l_chunk_idx < k_num_chunks; ++l_chunk_idx)
    {
        const uintptr_t lp_chunk = l_chunk_ptrs[l_chunk_idx];
        if (!is_valid_ptr(lp_chunk))
            continue;

        // Single bulk RPM for the whole chunk
        if (!R().ReadRaw(lp_chunk, l_chunk_buf, k_chunk_bytes))
            continue;

        const uint32_t l_base_index = l_chunk_idx * k_slots_per_chunk;

        for (uint32_t l_slot = 0; l_slot < k_slots_per_chunk; ++l_slot)
        {
            const uint32_t l_entity_index = l_base_index + l_slot;

            // Skip slot 0 (world), cap at max
            if (l_entity_index == 0 || l_entity_index > k_max_entities)
                continue;

            const uint8_t* lp_identity =
                l_chunk_buf + k_entity_identity_size * l_slot;

            // ── Parse from local buffer — no RPM ─────────────
            uint32_t l_handle = 0;
            std::memcpy(&l_handle, lp_identity + k_identity_handle, sizeof(l_handle));

            if ((l_handle & 0x7FFF) != l_entity_index)
                continue;   // slot is free or serial mismatch

            uintptr_t lp_instance = 0;
            std::memcpy(&lp_instance, lp_identity, sizeof(lp_instance));

            if (!is_valid_ptr(lp_instance) || lp_instance == m_local_controller)
                continue;

            // ── is_class only on candidates that survived ─────
            //
            //  Slots 1–64  → player controllers
            //  Slots 65+   → weapons / projectiles / etc.
            //
            //  Tip: if you trust CS2's layout you can skip is_class
            //  for slots 1–64 entirely and save ~4 RPM each.

            if (l_entity_index <= 64)
            {
                m_players.push_back(lp_instance);
            }
            else
            {
                if (is_class(lp_instance, "CWeaponBase") ||
                    is_class(lp_instance, "CC4"))
                    m_weapons.push_back(lp_instance);
                // extend for chickens, nades, etc.
            }
        }
    }

    return !m_players.empty();
}

// ── Controllers ───────────────────────────────────────────────
//
//  Player slots 1–64 in the entity list are CCSPlayerControllers.
//  refresh() stores their m_pInstance pointers directly — no further
//  validation needed inside the accessors.

const std::vector<uintptr_t>& CEntityCache::get_controllers() const
{
    return m_players;
}

// ── Pawns ─────────────────────────────────────────────────────
//
//  Each controller holds a CHandle<CCSPlayerPawn> at +0x7C0.
//  We decode the handle and walk the entity list to retrieve the pawn pointer.

std::vector<uintptr_t> CEntityCache::get_pawns() const
{
    std::vector<uintptr_t> l_pawns;
    l_pawns.reserve(m_players.size());

    for (const uintptr_t lp_controller : m_players)
    {
        const auto  l_pawn_handle = R().ReadMem<uint32_t>(
            lp_controller + SCHEMA_OFFSET(CBasePlayerController, m_hPawn));

        const uintptr_t lp_pawn = resolve_entity_from_handle(l_pawn_handle);
        if (lp_pawn)
            l_pawns.push_back(lp_pawn);
    }

    return l_pawns;
}

// ── Controller ↔ Pawn pair ────────────────────────────────────
//
//  Convenience: returns paired {controller, pawn} for every slot
//  where both pointers resolved successfully.

std::vector<CEntityCache::EntityPair> CEntityCache::get_player_pairs() const
{
    std::vector<EntityPair> l_pairs;
    l_pairs.reserve(m_players.size());

    for (const uintptr_t lp_controller : m_players)
    {
        const auto  l_pawn_handle = R().ReadMem<uint32_t>(
            lp_controller + SCHEMA_OFFSET(CBasePlayerController, m_hPawn));

        const uintptr_t lp_pawn = resolve_entity_from_handle(l_pawn_handle);
        if (lp_pawn)
            l_pairs.push_back({ lp_controller, lp_pawn });
    }

    return l_pairs;
}

// ── Misc ──────────────────────────────────────────────────────

const std::vector<uintptr_t>& CEntityCache::get_players() const
{
    return m_players;
}

int32_t CEntityCache::get_player_count() const
{
    return static_cast<int32_t>(m_players.size());
}

const std::vector<uintptr_t> & CEntityCache::get_weapons() const
{
    return m_weapons;
}

// ── Iterate ───────────────────────────────────────────────────
//
//  Walks the CEntityIdentity linked list (all live entities, not just
//  player slots). Skips slots whose handle lower word is 0xFFFF (free/invalid).
//  Resolves m_pInstance (at identity + 0x00) before passing to callback,
//  consistent with what refresh() stores in m_entities.
//
//  Callback returns true to continue, false to stop early.

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
            // Resolve m_pInstance (offset 0x00 on CEntityIdentity)
            // so callers receive the same pointer type as get_controllers() etc.
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

// ── Class name ────────────────────────────────────────────────

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

    // Strip Itanium ABI length prefix (e.g. "19CCSPlayerController" → "CCSPlayerController")
    const auto l_start = l_name.find_first_not_of("0123456789");
    if (l_start != std::string::npos)
        l_name = l_name.substr(l_start);

    return l_name;
}

bool CEntityCache::is_class(uintptr_t a_p_identity, const char* a_name) const
{
    const std::string l_classname = get_classname(a_p_identity);
    return !l_classname.empty() && l_classname == a_name;
}

uintptr_t CEntityCache::get_controller_at_index(uint32_t a_index) const
{
    if (!is_valid_ptr(m_p_entity_list))
        return 0;

    const uint32_t  l_bucket_index    = a_index >> 9;
    const uint32_t  l_index_in_bucket = a_index & 0x1FF;

    const auto lp_chunk = R().ReadMem<uintptr_t>(
        m_p_entity_list + sizeof(uintptr_t) * l_bucket_index);

    if (!is_valid_ptr(lp_chunk))
        return 0;

    const uintptr_t lp_identity = lp_chunk + k_entity_identity_size * l_index_in_bucket;

    // Validate handle slot matches requested index
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

uintptr_t CEntityCache::resolve_weapon_from_handle(uint32_t handle) const
{
    if (!handle)
        return 0;

    const uint32_t index  = handle & 0xFFF;    // 12-bit index for weapon handles
    const uint32_t bucket = index >> 9;
    const uint32_t offset = index & 0x1FF;

    const auto chunk = R().ReadMem<uintptr_t>(
        m_p_entity_list + sizeof(uintptr_t) * bucket);
    if (!is_valid_ptr(chunk))
        return 0;

    const auto entity = R().ReadMem<uintptr_t>(
        chunk + k_entity_identity_size * offset);

    return is_valid_ptr(entity) ? entity : 0;
}