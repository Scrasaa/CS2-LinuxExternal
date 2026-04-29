//
// Created by scrasa on 23.02.26.
//

#ifndef CS2_LINUXEXTERNAL_DEFS_H
#define CS2_LINUXEXTERNAL_DEFS_H
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
//  Remote layout constants  (CS2 Linux, CUtlTSHashV2) reversed through libschemasystem.so
// ─────────────────────────────────────────────────────────────────────────────

static constexpr uintptr_t k_sentinel             = 0xDDDDDDDDDDDDDDDD;

// ── CUtlVector ───────────────────────────────────────────────────────────────
static constexpr uint32_t k_utlvec_ptr            = 0x00;  // T** m_Memory.m_pMemory
static constexpr uint32_t k_utlvec_size           = 0x10;  // int32_t m_Size

// ── CUtlMemoryPoolBase  (first member of CUtlTSHashV2) ───────────────────────
static constexpr uint32_t k_pool_blocks_allocated = 0x08;
static constexpr uint32_t k_pool_peak_alloc       = 0x10;
static constexpr uint32_t k_pool_free_head        = 0x20;

// ── CUtlTSHashV2 ─────────────────────────────────────────────────────────────
static constexpr uint32_t k_bucket_first_uncommit = 0x10; // m_pFirstUncommitted within bucket
static constexpr uint32_t k_bucket_array_start    = 0x28;   // bucket[0] first_uncommit from buckets base
static constexpr uint32_t k_hash_buckets_offset   = 0x90;//0x24; // Linux: +0x20 pool + 0x04 pad
static constexpr int32_t  k_bucket_count          = 1024;//256;

static constexpr uint32_t k_bucket_stride         = 0x18;//0x20; // Linux HashBucket_t size

// ── HashFixedData_t ───────────────────────────────────────────────────────────
static constexpr uint32_t k_node_next             = 0x08;
static constexpr uint32_t k_node_data             = 0x10;

// ── HashAllocatedBlob_t ───────────────────────────────────────────────────────
static constexpr uint32_t k_blob_next             = 0x00;
static constexpr uint32_t k_blob_data             = 0x10;

// ── CSchemaSystem ────────────────────────────────────────────────────────────
static constexpr uint32_t k_system_scope_vec      = 0x1F8; // CUtlVector<CSchemaSystemTypeScope*>

// ── CSchemaSystemTypeScope ─────────────────────────────────────────────────── Update!
static constexpr uint32_t k_scope_name            = 0x08;  // char[256]
static constexpr uint32_t k_scope_class_count     = 0x472; // uint16_t m_classCount
static constexpr uint32_t k_scope_class_table     = 0x478; // SchemaClassEntry* m_classTable
static constexpr uint32_t k_scope_class_hash      = 0x560;//0x498; // m_ClassBindings (confirmed)

// ── SchemaClassEntry  (24-byte stride, class ptr at +0x10) ───────────────────
static constexpr uint32_t k_class_entry_ptr_off   = 0x10;
static constexpr uint32_t k_class_entry_stride    = 0x18;

// ── SchemaClassInfoData_t ─────────────────────────────────────────────────────

static constexpr uint32_t k_class_self_ptr   = 0x00; // SchemaClassInfoData_t* m_pSelf
static constexpr uint32_t k_class_name_ptr   = 0x08; // const char*
static constexpr uint32_t k_class_module_ptr = 0x10; // const char* (binaryName in practice)
static constexpr uint32_t k_class_sizeof     = 0x20; // int32_t m_nSize (NOT 0x18)
static constexpr uint32_t k_class_field_size = 0x24; // int16_t m_nFieldSize
static constexpr uint32_t k_class_alignof    = 0x26; // uint8_t alignment (was shifted earlier)
static constexpr uint32_t k_class_base_count = 0x27; // int8_t m_nBaseClassSize
static constexpr uint32_t k_class_field_arr  = 0x30; // SchemaClassFieldData_t*
static constexpr uint32_t k_class_base_arr   = 0x38; // SchemaBaseClassInfoData_t*

// ── SchemaClassFieldData_t  (remote stride = 0x20) ───────────────────────────
//   +0x00  const char*   m_pszName
//   +0x08  void*         m_pSchemaType
//   +0x10  int32_t       m_nSingleInheritanceOffset
//   +0x14  int32_t       m_nMetadataSize
//   +0x18  void*         m_pMetadata
//   +0x20  (next field)
static constexpr uint32_t k_field_name_ptr        = 0x00;
static constexpr uint32_t k_field_type_ptr        = 0x08;
static constexpr uint32_t k_field_inh_offset      = 0x10;
static constexpr uint32_t k_field_meta_size       = 0x14;

static constexpr uint32_t k_field_stride          = 0x20;

// ── SchemaBaseClassInfoData_t  (remote stride = 0x10) ────────────────────────
//   +0x00  uint32_t               m_unOffset
//   +0x04  char[4]                pad
//   +0x08  SchemaClassInfoData_t* m_pClass    <- direct pointer, NOT double-pointer
static constexpr uint32_t k_base_inh_offset       = 0x00; // uint32_t
static constexpr uint32_t k_base_class_ptr        = 0x08; // SchemaClassInfoData_t* (direct)
static constexpr uint32_t k_binding_class_ptr   = 0x20;

static constexpr uint32_t k_base_stride           = 0x10;

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

/*      CEntityIdentity

        constexpr std::ptrdiff_t m_nameStringTableIndex = 0x14; // int32
        constexpr std::ptrdiff_t m_name = 0x18; // CUtlSymbolLarge
        constexpr std::ptrdiff_t m_designerName = 0x20; // CUtlSymbolLarge
        constexpr std::ptrdiff_t m_flags = 0x30; // uint32
        constexpr std::ptrdiff_t m_worldGroupId = 0x38; // WorldGroupId_t
        constexpr std::ptrdiff_t m_fDataObjectTypes = 0x3C; // uint32
        constexpr std::ptrdiff_t m_PathIndex = 0x40; // ChangeAccessorFieldPathIndex_t
        constexpr std::ptrdiff_t m_pAttributes = 0x48; // CEntityAttributeTable*
        constexpr std::ptrdiff_t m_pPrev = 0x50; // CEntityIdentity*
        constexpr std::ptrdiff_t m_pNext = 0x58; // CEntityIdentity*
        constexpr std::ptrdiff_t m_pPrevByClass = 0x60; // CEntityIdentity*
        constexpr std::ptrdiff_t m_pNextByClass = 0x68; // CEntityIdentity*
*/

#endif //CS2_LINUXEXTERNAL_DEFS_H