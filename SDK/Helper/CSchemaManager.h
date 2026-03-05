#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// SCHEMA macro  —  EXTERNAL process edition
//
// We have no direct pointer to the remote object, only a remote address.
// The macro therefore takes the remote base address explicitly and issues
// a ReadMem call through CUtils rather than dereferencing a local pointer.
//
// Usage (call-site, NOT inside a class definition):
//
//   uintptr_t p_entity = ...;   // remote address of a C_BaseEntity
//
//   int32_t hp     = SCHEMA_GET(p_entity, C_BaseEntity, m_iHealth,   int32_t);
//   Vector  origin = SCHEMA_GET(p_entity, C_BaseEntity, m_vecOrigin, Vector);
//
//   // Write back:
//   SCHEMA_SET(p_entity, C_BaseEntity, m_iHealth, int32_t, 100);
//
// The offset is resolved once and cached in a static local (thread-safe in
// C++11+). Subsequent calls pay only a single ReadMem/WriteMem.
// ─────────────────────────────────────────────────────────────────────────────
#define SCHEMA_GET(remote_base, class_name, field_name, type)                    \
    [&]() -> type                                                                \
    {                                                                            \
        static const uint32_t s_off =                                            \
            CSchemaManager::Get().GetSchemaOffset(#class_name, #field_name);     \
        return CUtils::Get().ReadMem<type>((uintptr_t)(remote_base) + s_off);    \
    }()

#define SCHEMA_SET(remote_base, class_name, field_name, type, value)            \
    [&]()                                                                       \
    {                                                                           \
        static const uint32_t s_off =                                           \
            CSchemaManager::Get().GetSchemaOffset(#class_name, #field_name);    \
        CUtils::Get().WriteMem<type>((remote_base) + s_off, (value));           \
    }()

// Convenience: returns the cached offset directly (useful when you need
// to pass the offset around or do arithmetic on it yourself).
#define SCHEMA_OFFSET(class_name, field_name)                                   \
    []() -> uint32_t                                                            \
    {                                                                           \
        static const uint32_t s_off =                                           \
            CSchemaManager::Get().GetSchemaOffset(#class_name, #field_name);    \
        return s_off;                                                           \
    }()

// ─────────────────────────────────────────────────────────────────────────────
// CSchemaManager
// ─────────────────────────────────────────────────────────────────────────────
class CSchemaManager
{
public:
    // ── Singleton ────────────────────────────────────────────────────────────
    static CSchemaManager& Get();

    CSchemaManager(const CSchemaManager&)            = delete;
    CSchemaManager& operator=(const CSchemaManager&) = delete;

    // ── Initialisation ───────────────────────────────────────────────────────
    // Must be called once after CUtils::Init() and schema_system is resolved.
    void Init(uintptr_t schema_system);

    // ── Offset lookup ────────────────────────────────────────────────────────
    // Returns the flat absolute offset of `field_name` inside `class_name`,
    // including inherited base-class offsets. Returns 0 on failure.
    uint32_t GetSchemaOffset(const std::string& class_name,
                             const std::string& field_name,
                             const std::string& scope_name = "libclient.so") const;

    // ── Dump functions ───────────────────────────────────────────────────────

    // Writes every scope name, address, and class count.
    void DumpAllScopes(const std::string& path) const;

    // Writes an enum-style list of every class name across all scopes,
    // annotated with size and field count.
    void DumpAllClasses(const std::string& path) const;

    // Writes a full flat field layout for every class in every scope,
    // with absolute offsets (inherited bases resolved, same order as
    // DumpClassFields / your Source 1 DumpNetVars).
    void DumpAllClassFields(const std::string& path) const;

private:
    CSchemaManager() = default;

    // ── Types ─────────────────────────────────────────────────────────────────
    struct SchemaField
    {
        std::string name;
        uint32_t    abs_offset{ 0 };
    };

    struct SchemaClass
    {
        std::string              name;
        std::string              scope;
        int32_t                  class_size{ 0 };
        int16_t                  field_count{ 0 };
        uint8_t                  base_count{ 0 };
        uintptr_t                p_remote{ 0 };         // remote SchemaClassInfoData_t*
        std::vector<SchemaField> fields;
    };

    // key: class_name → SchemaClass (with flat field list built at Init time)
    using ClassMap = std::unordered_map<std::string,
    std::unordered_map<std::string, SchemaClass>>;

    // ── Internal helpers ─────────────────────────────────────────────────────
    bool collect_scopes(std::vector<std::pair<std::string, uintptr_t>>& out) const;

    // Recursively builds a flat field list for p_class, prepending
    // base_offset to every inherited and own field.
    void collect_fields(uintptr_t p_class, std::vector<SchemaField>& out_fields) const;

    // Reads SchemaClassInfoData_t from remote and inserts into m_classes.
    void register_class(uintptr_t p_class, const std::string& scope_name);

    // Walks CUtlTSHashV2 and registers all classes found.
    void walk_hash(uintptr_t p_hash, const std::string& scope_name);

    // File-writing helpers used by Dump* functions.
    void write_base_classes(std::ofstream& f,
                            uintptr_t p_base_arr,
                            uint8_t   base_count,
                            int       indent_spaces) const;

    void write_class_block(std::ofstream& f, const SchemaClass& sc) const;

    static std::string make_timestamp_header();

    // ── State ────────────────────────────────────────────────────────────────
    uintptr_t m_schema_system{ 0 };
    ClassMap  m_classes;
};