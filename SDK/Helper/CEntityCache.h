#pragma once
#include <cstdint>
#include <vector>
#include <functional>
#include <string>

enum EPlayerFlags : uint8_t
{
    FLAG_DEFUSING    = 1 << 0,
    FLAG_PLANTING    = 1 << 1,
    FLAG_SCOPED      = 1 << 2,
    FLAG_FLASHED     = 1 << 3,
    FLAG_HAS_C4      = 1 << 4,
    FLAG_HAS_DEFUSER = 1 << 5,
    FLAG_HAS_HELMET  = 1 << 6
};
// maybe we need localPlayerInfo?
struct PlayerInfo
{
    std::string szName{"Unknown"};
    int iHealth{};
    int iMaxHealth{};
    int iArmor{};
    uint8_t flags{};
    int iMoney{}; // remove
    std::string szActiveWeaponName{"Unknown"};
    uint8_t bLifeState{};
    int iTeamNum{};
    int iReserveAmmo{};
    int iClipPrimary{};

    [[nodiscard]] bool HasFlag(const uint8_t f) const
    {
        return (flags & f) != 0;
    }

    void SetFlag(const uint8_t f)
    {
        flags |= f;
    }

    void ClearFlag(const uint8_t f)
    {
        flags &= ~f;
    }
};

class CEntityCache
{
public:
    uintptr_t m_localplayer_ptr = 0;
    uintptr_t m_local_controller = 0;
    uintptr_t m_local_pawn = 0;

    // Paired controller + pawn, both guaranteed non-null
    struct EntityPair
    {
        uintptr_t p_controller;
        uintptr_t p_pawn;
    };

    // Result type for iterate() — avoids ambiguous bool returns
    enum class IterResult
    {
        Completed,    // callback ran for every live entity
        Aborted,      // callback returned false to stop early
        InvalidList   // m_p_entity_list failed pointer validation
    };

    explicit CEntityCache(uintptr_t a_p_entity_list);

    // Populates m_entities with CCSPlayerController ptrs for slots 1–64
    [[nodiscard]] bool refresh();

    // Returns raw controller pointers (same as m_entities)
    [[nodiscard]] const std::vector<uintptr_t>& get_controllers()   const;
    // Resolves m_hPlayerPawn for each controller and returns pawn pointers
    [[nodiscard]] std::vector<uintptr_t>        get_pawns()         const;

    // Returns {controller, pawn} pairs where both resolved successfully
    [[nodiscard]] std::vector<EntityPair>       get_player_pairs()  const;

    // Legacy / generic accessors
    [[nodiscard]] const std::vector<uintptr_t>& get_players()      const;
    [[nodiscard]] int32_t                        get_player_count()         const;

    [[nodiscard]] const std::vector<uintptr_t>& get_weapons()      const;

    // Walk the alive-entity linked list via the CEntityIdentity linked list.
    // Note: iterate() traverses ALL live entities (not just player slots 1-64
    // like refresh() does), passing the resolved m_pInstance to the callback.
    // Callback signature: bool(int32_t index, uintptr_t p_instance)
    //   return true  → continue
    //   return false → stop early  (IterResult::Aborted)
    [[nodiscard]] IterResult iterate(const std::function<bool(int32_t, uintptr_t)>& a_fn) const;

    // Schema class name helpers
    [[nodiscard]] std::string get_classname(uintptr_t a_p_entity_instance) const;
    [[nodiscard]] bool        is_class(uintptr_t a_p_identity, const char* a_name) const;

    [[nodiscard]] uintptr_t resolve_entity_from_handle(uint32_t a_handle) const;
    [[nodiscard]] uintptr_t get_controller_at_index(uint32_t a_index) const;
    [[nodiscard]] uintptr_t read_entity_at_index(uint32_t a_index)   const;

    uintptr_t resolve_weapon_from_handle(uint32_t handle) const;
public:
    uintptr_t               m_p_entity_list;
    std::vector<uintptr_t>  m_players;         // CCSPlayerController m_pInstance ptrs
    std::vector<uintptr_t>  m_weapons;         // C_BasePlayerWeapon m_pInstance ptrs

    [[nodiscard]] uintptr_t read_list_head()                         const;
};

extern CEntityCache g_EntityCache;