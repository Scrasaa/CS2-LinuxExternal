#pragma once
#include <cstdint>
#include <vector>
#include <functional>
#include <string>

// ── Confirmed layout ─────────────────────────────────────────
//   entity_list              = resolved via pattern scan in libclient.so
//                              "48 8D 05 ? ? ? ? 48 8B 00 48 83 C0"
//                              GetAbsoluteAddress(hit, 3, 7)
//
//   entity_list + 0x200      = CEntityIdentity*  linked list head
//   CEntityIdentity + 0x00   = vtable ptr
//   CEntityIdentity + 0x10   = CEntityHandle     (u32)
//   CEntityIdentity + 0x58   = CEntityIdentity*  pNext
//   CEntityIdentity + 0x10   = schema binding ptr
//   schema_binding  + 0x20   = const char*        class name
// ─────────────────────────────────────────────────────────────

static constexpr uintptr_t  k_entity_list_head_offset  = 0x200; // CEntityIdentity* head of alive linked list
static constexpr uintptr_t  k_identity_handle          = 0x10;  // CEntityHandle (u32)
static constexpr uintptr_t  k_identity_next            = 0x58;  // CEntityIdentity* pNext
static constexpr uintptr_t  k_identity_classname_ptr   = 0x10;  // schema binding ptr on CEntityIdentity
static constexpr uintptr_t  k_classname_ptr_name       = 0x20;  // const char* inside schema binding
static constexpr uintptr_t  k_entity_identity_size     = 0x70;  // sizeof(CEntityIdentity)

class CEntityCache
{
public:
    uintptr_t m_p_localplayer = 0;
    uintptr_t m_p_localplayer_controller = 0;

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
    [[nodiscard]] std::vector<EntityPair>       get_entity_pairs()  const;

    // Legacy / generic accessors
    [[nodiscard]] const std::vector<uintptr_t>& get_entities()      const;
    [[nodiscard]] int32_t                        get_count()         const;

    // Walk the alive-entity linked list via the CEntityIdentity linked list.
    // Note: iterate() traverses ALL live entities (not just player slots 1-64
    // like refresh() does), passing the resolved m_pInstance to the callback.
    // Callback signature: bool(int32_t index, uintptr_t p_instance)
    //   return true  → continue
    //   return false → stop early  (IterResult::Aborted)
    [[nodiscard]] IterResult iterate(std::function<bool(int32_t, uintptr_t)> a_fn) const;

    // Schema class name helpers
    [[nodiscard]] std::string get_classname(uintptr_t a_p_entity_instance) const;
    [[nodiscard]] bool        is_class(uintptr_t a_p_identity, const char* a_name) const;

    [[nodiscard]] uintptr_t resolve_entity_from_handle(uint32_t a_handle) const;
private:
    uintptr_t               m_p_entity_list;
    std::vector<uintptr_t>  m_entities;         // CCSPlayerController m_pInstance ptrs

    [[nodiscard]] uintptr_t read_list_head()                         const;
    [[nodiscard]] uintptr_t read_entity_at_index(uint32_t a_index)   const;

};

extern CEntityCache g_EntityCache;