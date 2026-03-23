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

CEntityCache::CEntityCache(uintptr_t a_p_entity_list)
    : m_p_entity_list(a_p_entity_list)
{
    m_entities.reserve(64);
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

bool CEntityCache::refresh()
{
    m_entities.clear();

    if (!is_valid_ptr(m_p_entity_list))
        return false;

    m_p_localplayer_controller = R().ReadMem<uintptr_t>(m_p_localplayer);

    if (!is_valid_ptr(m_p_localplayer_controller))
        return false;

    for (uint32_t i = 1; i <= 64; ++i)
    {
        const uint32_t  l_bucket_index    = i >> 9;
        const uint32_t  l_index_in_bucket = i & 0x1FF;

        const auto lp_chunk = R().ReadMem<uintptr_t>(
            m_p_entity_list + sizeof(uintptr_t) * l_bucket_index);

        if (!is_valid_ptr(lp_chunk))
            continue;

        const auto lp_instance = R().ReadMem<uintptr_t>(
            lp_chunk + k_entity_identity_size * l_index_in_bucket);

        if (!is_valid_ptr(lp_instance))
            continue;

        // Read handle from identity (+0x10), validate slot matches
        const uintptr_t lp_identity = lp_chunk + k_entity_identity_size * l_index_in_bucket;
        const auto  l_handle    = R().ReadMem<uint32_t>(lp_identity + k_identity_handle);

        if ((l_handle & 0x7FFF) != i)
            continue;

        const auto lp_controller = R().ReadMem<uintptr_t>(lp_identity);
        if (!is_valid_ptr(lp_controller))
            continue;

        if (lp_controller == m_p_localplayer_controller)
            continue;

        // Validate it’s a controller - later sort it
        if (!is_class(lp_controller, "CCSPlayerController"))
            continue;

        m_entities.push_back(lp_controller);
    }

    return !m_entities.empty();
}

// ── Controllers ───────────────────────────────────────────────
//
//  Player slots 1–64 in the entity list are CCSPlayerControllers.
//  refresh() stores their m_pInstance pointers directly — no further
//  validation needed inside the accessors.

const std::vector<uintptr_t>& CEntityCache::get_controllers() const
{
    return m_entities;
}

// ── Pawns ─────────────────────────────────────────────────────
//
//  Each controller holds a CHandle<CCSPlayerPawn> at +0x7C0.
//  We decode the handle and walk the entity list to retrieve the pawn pointer.

std::vector<uintptr_t> CEntityCache::get_pawns() const
{
    std::vector<uintptr_t> l_pawns;
    l_pawns.reserve(m_entities.size());

    for (const uintptr_t lp_controller : m_entities)
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

std::vector<CEntityCache::EntityPair> CEntityCache::get_entity_pairs() const
{
    std::vector<EntityPair> l_pairs;
    l_pairs.reserve(m_entities.size());

    for (const uintptr_t lp_controller : m_entities)
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

const std::vector<uintptr_t>& CEntityCache::get_entities() const
{
    return m_entities;
}

int32_t CEntityCache::get_count() const
{
    return static_cast<int32_t>(m_entities.size());
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
    std::function<bool(int32_t, uintptr_t)> a_fn) const
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