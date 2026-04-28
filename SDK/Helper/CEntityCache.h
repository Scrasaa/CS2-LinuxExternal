#pragma once

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
//  Cached player pipeline:
//      refresh() → m_players (all controllers, slots 1-64)
//               → m_cached_players (alive + non-dormant, with full PlayerInfo)
//
//  CESP::Run() iterates m_cached_players exclusively — no per-frame
//  game-state reads needed in the render path.
//
// ─────────────────────────────────────────────────────────────

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <unordered_set>

#include "SDK/player_info.h"

class CEntityCache
{
public:
    // ── Public addresses ──────────────────────────────────────

    uintptr_t m_localplayer_ptr    = 0;
    uintptr_t m_local_controller   = 0;
    uintptr_t m_local_pawn         = 0;
    int32_t   m_local_team         = 0;   // cached in refresh() for CESP team filter

    // ── Paired controller + pawn (both guaranteed non-null) ──

    struct EntityPair
    {
        uintptr_t p_controller;
        uintptr_t p_pawn;
    };

    // ── Fully-populated per-player snapshot ──────────────────
    //
    //  Built by refresh() — contains only alive, non-dormant players.
    //  p_game_scene is cached so CESP can drive bone reads without an
    //  extra RPM call per player.

    struct CachedPlayer
    {
        uintptr_t  p_controller  = 0;
        uintptr_t  p_pawn        = 0;
        uintptr_t  p_game_scene  = 0;
        PlayerInfo info;
    };

    // ── Result type for iterate() ─────────────────────────────
    //
    //  Avoids ambiguous bool returns from a callback-driven walk.

    enum class IterResult
    {
        Completed,    // callback ran for every live entity
        Aborted,      // callback returned false to stop early
        InvalidList,  // m_p_entity_list failed pointer validation
    };

    // ── Construction ─────────────────────────────────────────

    explicit CEntityCache(uintptr_t a_p_entity_list);

    // ── Primary refresh ──────────────────────────────────────
    //
    //  Pass 1 : bulk-reads chunk arrays → populates m_players + m_weapons
    //  Pass 2 : resolves pawns, filters dead/dormant → populates m_cached_players
    //
    //  Returns false if the local player could not be resolved (skip frame).

    [[nodiscard]] bool refresh();

    // ── Cached-player accessor (primary CESP interface) ──────

    [[nodiscard]] const std::vector<CachedPlayer>&  get_cached_players() const;

    // ── Raw controller / pawn accessors ──────────────────────

    [[nodiscard]] const std::vector<uintptr_t>& get_controllers()  const;
    [[nodiscard]] std::vector<uintptr_t>        get_pawns()        const;
    [[nodiscard]] std::vector<EntityPair>       get_player_pairs() const;

    // ── Misc accessors ────────────────────────────────────────

    [[nodiscard]] const std::vector<uintptr_t>& get_players()      const;
    [[nodiscard]] int32_t                        get_player_count() const;
    [[nodiscard]] const std::vector<uintptr_t>& get_weapons()      const;

    // ── Linked-list walk ──────────────────────────────────────
    //
    //  Traverses ALL live entities (not just player slots 1-64).
    //  Callback: bool(int32_t index, uintptr_t p_instance)
    //    true  → continue  |  false → stop early (IterResult::Aborted)

    [[nodiscard]] IterResult iterate(
        const std::function<bool(int32_t, uintptr_t)>& a_fn) const;

    // ── Class-name helpers ────────────────────────────────────

    [[nodiscard]] std::string get_classname(uintptr_t a_p_entity_instance) const;
    [[nodiscard]] bool        is_class(uintptr_t a_p_identity, const char* a_name) const;

    // ── Handle / index resolution ─────────────────────────────

    [[nodiscard]] uintptr_t resolve_entity_from_handle(uint32_t a_handle) const;
    [[nodiscard]] uintptr_t resolve_weapon_from_handle(uint32_t handle)   const;
    [[nodiscard]] uintptr_t get_controller_at_index(uint32_t a_index)     const;
    [[nodiscard]] uintptr_t read_entity_at_index(uint32_t a_index)        const;

private:

    // ── CEntityCache.h additions ──────────────────────────────────
    //
    //  Add these to your existing CEntityCache class definition.
    //  Public API and CachedPlayer / PlayerInfo structs are unchanged.
    //
    // ─────────────────────────────────────────────────────────────

    // Cached bucket pointers — filled once per frame with one ReadRaw at
    // the top of refresh(). All handle resolution reuses this array.
    // k_num_chunks = 2 for MAX_ENTITY=1024, slots_per_chunk=512.
    static constexpr uint32_t k_chunk_cache_size = (1024u + 512u - 1u) / 512u;
    uintptr_t m_chunk_ptrs[k_chunk_cache_size] = {};

    // Weapon vtable address cache — populated on first RTTI walk per vtable,
    // then reused every subsequent frame. Stable for the game module lifetime.
    mutable std::unordered_set<uintptr_t> m_weapon_vtable_set;

    // ── Private methods to add ────────────────────────────────────

    // Returns true if a_p_instance is a weapon entity.
    // Warm path: 1 RPM + O(1) hash lookup.
    // Cold path: 4 RPM RTTI walk (once per unique weapon vtable, ever).
    bool is_weapon_entity(uintptr_t a_p_instance) const;

public:
    uintptr_t              m_p_entity_list;
    std::vector<uintptr_t> m_players;         // CCSPlayerController m_pInstance ptrs (slots 1-64)
    std::vector<uintptr_t> m_weapons;         // C_BasePlayerWeapon  m_pInstance ptrs
    std::vector<CachedPlayer> m_cached_players; // alive + non-dormant players with full info

    [[nodiscard]] uintptr_t read_list_head() const;
};

extern CEntityCache g_EntityCache;