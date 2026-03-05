#include "CSchemaManager.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

#include "../../Utils/Utils.h"

// ─────────────────────────────────────────────────────────────────────────────
// CSchemaManager — singleton
// ─────────────────────────────────────────────────────────────────────────────
CSchemaManager& CSchemaManager::Get()
{
    static CSchemaManager s_instance;
    return s_instance;
}

// ─────────────────────────────────────────────────────────────────────────────
// Init
// ─────────────────────────────────────────────────────────────────────────────
void CSchemaManager::Init(uintptr_t schema_system)
{
    m_schema_system = schema_system;
    m_classes.clear();

    std::vector<std::pair<std::string, uintptr_t>> scopes;
    if (!collect_scopes(scopes))
    {
        std::cerr << "[-] CSchemaManager::Init — failed to collect scopes.\n";
        return;
    }

    for (const auto& [scope_name, p_scope] : scopes)
    {
        const uint16_t  class_count = R().ReadMem<uint16_t>(p_scope + k_scope_class_count);
        const uintptr_t class_table = R().ReadMem<uintptr_t>(p_scope + k_scope_class_table);

        if (class_count > 0 && is_valid_ptr(class_table))
        {
            for (uint16_t j = 0; j < class_count; ++j)
            {
                const uintptr_t entry_base = class_table
                    + static_cast<uintptr_t>(j) * k_class_entry_stride;
                const uintptr_t p_binding  = R().ReadMem<uintptr_t>(entry_base + k_class_entry_ptr_off);
                if (!is_valid_ptr(p_binding))
                    continue;

                const uintptr_t p_class = R().ReadMem<uintptr_t>(p_binding + k_binding_class_ptr);
                register_class(p_class, scope_name);
            }
        }
        else
        {
            walk_hash(p_scope + k_scope_class_hash, scope_name);
        }
    }

    size_t total = 0;
    for (const auto& [scope, class_map] : m_classes)
        total += class_map.size();
    printf("[+] CSchemaManager::Init — %zu classes registered across %zu scopes.\n",
        total, m_classes.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// GetSchemaOffset
// ─────────────────────────────────────────────────────────────────────────────
uint32_t CSchemaManager::GetSchemaOffset(const std::string& class_name,
                                          const std::string& field_name, const std::string& scope_name) const
{
    const auto scope_it = m_classes.find(scope_name);
    if (scope_it == m_classes.end()) return 0;

    const auto class_it = scope_it->second.find(class_name);
    if (class_it == scope_it->second.end()) return 0;

    for (const SchemaField& field : class_it->second.fields)
        if (field.name == field_name)
            return field.abs_offset;

    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// collect_scopes
// ─────────────────────────────────────────────────────────────────────────────
bool CSchemaManager::collect_scopes(
    std::vector<std::pair<std::string, uintptr_t>>& out) const
{
    if (!m_schema_system)
        return false;

    const uintptr_t vec_addr     = m_schema_system + k_system_scope_vec;
    const uintptr_t p_scope_data = R().ReadMem<uintptr_t>(vec_addr + k_utlvec_ptr);
    const int32_t   scope_count  = R().ReadMem<int32_t>(vec_addr + k_utlvec_size);

    if (!is_valid_ptr(p_scope_data) || scope_count <= 0 || scope_count > 64)
        return false;

    out.reserve(static_cast<size_t>(scope_count));

    for (int32_t i = 0; i < scope_count; ++i)
    {
        const uintptr_t p_scope = R().ReadMem<uintptr_t>(
            p_scope_data + static_cast<uintptr_t>(i) * sizeof(uintptr_t));

        if (!is_valid_ptr(p_scope))
            continue;

        const std::string scope_name = R().ReadString(p_scope + k_scope_name, 256);
        if (scope_name.empty() || scope_name.size() > 128)
            continue;

        bool name_ok = true;
        for (char c : scope_name)
        {
            if (static_cast<unsigned char>(c) < 0x20) { name_ok = false; break; }
        }
        if (!name_ok)
            continue;

        out.emplace_back(scope_name, p_scope);
    }

    return !out.empty();
}

// ─────────────────────────────────────────────────────────────────────────────
// collect_fields
// Builds a flat field list for p_class, resolving base-class offsets.
// ─────────────────────────────────────────────────────────────────────────────
void CSchemaManager::collect_fields(uintptr_t p_class, std::vector<SchemaField>& out_fields) const
{
    if (!is_valid_ptr(p_class))
        return;

    const int16_t   field_count = R().ReadMem<int16_t>(p_class + k_class_field_size);
    const uintptr_t p_fields    = R().ReadMem<uintptr_t>(p_class + k_class_field_arr);

    if (field_count <= 0 || field_count > 20000 || !is_valid_ptr(p_fields))
        return;

    for (int16_t i = 0; i < field_count; ++i)
    {
        const uintptr_t p_field      = p_fields + static_cast<uintptr_t>(i) * k_field_stride;
        const uintptr_t p_field_name = R().ReadMem<uintptr_t>(p_field + k_field_name_ptr);
        const int32_t   inh_offset   = R().ReadMem<int32_t>(p_field + k_field_inh_offset);

        if (!is_valid_str_ptr(p_field_name))
            continue;

        const std::string field_name = remote_str(p_field_name);
        if (field_name.empty())
            continue;

        out_fields.push_back({ field_name, static_cast<uint32_t>(inh_offset) });
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// register_class
// ─────────────────────────────────────────────────────────────────────────────
void CSchemaManager::register_class(uintptr_t p_class, const std::string& scope_name)
{
    if (!is_valid_ptr(p_class))
        return;

    const uintptr_t p_name_ptr = R().ReadMem<uintptr_t>(p_class + k_class_name_ptr);
    if (!is_valid_str_ptr(p_name_ptr))
        return;

    const std::string class_name = remote_str(p_name_ptr);
    if (class_name.empty())
        return;

    // Allow re-registration from different scopes
    if (m_classes[scope_name].count(class_name))
        return;

    SchemaClass sc;
    sc.name        = class_name;
    sc.scope       = scope_name;
    sc.p_remote    = p_class;
    sc.class_size  = R().ReadMem<int32_t>(p_class + k_class_sizeof);
    sc.field_count = R().ReadMem<int16_t>(p_class + k_class_field_size);
    sc.base_count  = R().ReadMem<uint8_t>(p_class + k_class_base_count);

    collect_fields(p_class, sc.fields);

    m_classes[scope_name].emplace(class_name, std::move(sc));
}

// ─────────────────────────────────────────────────────────────────────────────
// walk_hash
// Walks CUtlTSHashV2: committed bucket chains then unallocated blob list.
// ─────────────────────────────────────────────────────────────────────────────
void CSchemaManager::walk_hash(uintptr_t p_hash, const std::string& scope_name)
{
    const int32_t   n_blocks_alloc  = R().ReadMem<int32_t>(p_hash + k_pool_blocks_allocated);
    const int32_t   n_peak_alloc    = R().ReadMem<int32_t>(p_hash + k_pool_peak_alloc);
    const int32_t   n_unalloc_count = n_peak_alloc - n_blocks_alloc;

    std::vector<uintptr_t> seen;
    seen.reserve(static_cast<size_t>(n_peak_alloc > 0 ? n_peak_alloc : 0));

    // ── Committed bucket chains ───────────────────────────────────────────────
    int32_t found = 0;
    const uintptr_t p_bucket_array = p_hash + k_hash_buckets_offset;
    for (int32_t b = 0; b < k_bucket_count; ++b)
    {
        //const uintptr_t p_bucket = p_buckets + static_cast<uintptr_t>(b) * k_bucket_stride;
        uintptr_t p_node = R().ReadMem<uintptr_t>(p_bucket_array + k_bucket_array_start + static_cast<uintptr_t>(b) * k_bucket_stride);

        while (is_valid_ptr(p_node) && found < n_blocks_alloc)
        {
            const uintptr_t p_class = R().ReadMem<uintptr_t>(p_node + k_node_data);
            if (is_valid_ptr(p_class))
            {
                seen.push_back(p_class);
                register_class(p_class, scope_name);
                ++found;
            }
            p_node = R().ReadMem<uintptr_t>(p_node + k_node_next);
        }
    }

    // ── Unallocated blob list ─────────────────────────────────────────────────
    if (n_unalloc_count > 0)
    {
        uintptr_t p_blob  = R().ReadMem<uintptr_t>(p_hash + k_pool_free_head);
        int32_t   ufound  = 0;

        while (is_valid_ptr(p_blob) && ufound < n_unalloc_count)
        {
            const uintptr_t p_binding = R().ReadMem<uintptr_t>(p_blob + k_blob_data);
            if (is_valid_ptr(p_binding))
            {
                const bool already_seen =
                    std::find(seen.begin(), seen.end(), p_binding) != seen.end();

                if (!already_seen)
                {
                    const uintptr_t p_class = R().ReadMem<uintptr_t>(p_binding + k_binding_class_ptr);
                    register_class(p_class, scope_name);
                    ++ufound;
                }
            }
            p_blob = R().ReadMem<uintptr_t>(p_blob + k_blob_next);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// make_timestamp_header
// ─────────────────────────────────────────────────────────────────────────────
std::string CSchemaManager::make_timestamp_header()
{
    time_t t = time(nullptr);
    struct tm now{};
    if (localtime_r(&t, &now) == nullptr)
        return "??:??:?? xx/xx/xxxx\n";

    char buf[32];
    std::snprintf(buf, sizeof(buf),
        "%02d:%02d:%02d %02d/%02d/%04d\n",
        now.tm_hour, now.tm_min, now.tm_sec,
        now.tm_mday, now.tm_mon + 1, now.tm_year + 1900);
    return buf;
}

// ─────────────────────────────────────────────────────────────────────────────
// write_base_classes
// ─────────────────────────────────────────────────────────────────────────────
void CSchemaManager::write_base_classes(std::ofstream& f,
                                         uintptr_t p_base_arr,
                                         uint8_t   base_count,
                                         int       indent_spaces) const
{
    if (!is_valid_ptr(p_base_arr) || base_count == 0 || base_count >= 32)
        return;

    const std::string prefix(indent_spaces, ' ');

    for (uint8_t b = 0; b < base_count; ++b)
    {
        const uintptr_t p_desc     = p_base_arr + b * k_base_stride;
        const uint32_t  inh_off    = R().ReadMem<uint32_t>(p_desc + k_base_inh_offset);
        const uintptr_t p_base_cls = R().ReadMem<uintptr_t>(p_desc + k_base_class_ptr);

        if (!is_valid_ptr(p_base_cls))
        {
            f << prefix << "// [base " << static_cast<int>(b)
              << "] @+0x" << std::hex << inh_off << std::dec << '\n';
            continue;
        }

        const uintptr_t p_base_name = R().ReadMem<uintptr_t>(p_base_cls + k_class_name_ptr);
        const std::string base_name = is_valid_ptr(p_base_name) ? remote_str(p_base_name) : "?";

        f << prefix
          << "// [base " << static_cast<int>(b) << "] "
          << std::left << std::setw(40) << base_name
          << " @+0x" << std::hex << inh_off << std::dec << '\n';
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// write_class_block
// ─────────────────────────────────────────────────────────────────────────────
void CSchemaManager::write_class_block(std::ofstream& f, const SchemaClass& sc) const
{
    const uintptr_t p_base_arr = R().ReadMem<uintptr_t>(sc.p_remote + k_class_base_arr);

    f << "// class " << sc.name
      << "  [size=0x" << std::hex << sc.class_size << std::dec
      << "  fields=" << sc.field_count
      << "  bases=" << static_cast<int>(sc.base_count) << "]\n";

    write_base_classes(f, p_base_arr, sc.base_count, 4);

    f << "{\n";
    for (const SchemaField& field : sc.fields)
    {
        f << "    /* +0x"
          << std::hex << std::setw(4) << field.abs_offset
          << std::dec << std::setfill(' ')
          << " */ " << field.name << '\n';
    }
    f << "};\n\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// DumpAllScopes
// ─────────────────────────────────────────────────────────────────────────────
void CSchemaManager::DumpAllScopes(const std::string& path) const
{
    std::ofstream f(path);
    if (!f.is_open()) { perror("[-] DumpAllScopes: failed to open file"); return; }

    f << make_timestamp_header();
    f << "// CS2 CSchemaSystem scope dump\n";
    f << "// CSchemaSystem @ 0x" << std::hex << m_schema_system << std::dec << "\n\n";

    std::vector<std::pair<std::string, uintptr_t>> scopes;
    if (!collect_scopes(scopes)) { f << "// ERROR: failed to collect scopes.\n"; return; }

    f << "// Total scopes: " << scopes.size() << "\n\n";

    int32_t idx = 0;
    for (const auto& [name, p_scope] : scopes)
    {
        const uint16_t  class_count = R().ReadMem<uint16_t>(p_scope + k_scope_class_count);
        const uintptr_t class_table = R().ReadMem<uintptr_t>(p_scope + k_scope_class_table);

        f << std::left
          << "[" << std::setw(2) << idx++ << "] "
          << std::setw(42) << name
          << " @ 0x" << std::hex << p_scope << std::dec
          << "  classes=" << class_count;

        if (!is_valid_ptr(class_table))
            f << "  (no class table — hash walk required)";

        f << '\n';
    }

    f.close();
    printf("[+] DumpAllScopes -> %s\n", path.c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
// DumpAllClasses
// ─────────────────────────────────────────────────────────────────────────────
void CSchemaManager::DumpAllClasses(const std::string& path) const
{
    std::ofstream f(path);
    if (!f.is_open()) { perror("[-] DumpAllClasses: failed to open file"); return; }

    f << make_timestamp_header();
    f << "// CS2 Schema class name dump\n\n";

    std::vector<std::pair<std::string, uintptr_t>> scopes;
    if (!collect_scopes(scopes)) { f << "// ERROR: failed to collect scopes.\n"; return; }

    for (const auto& [scope_name, p_scope] : scopes)
    {
        const uint16_t  class_count = R().ReadMem<uint16_t>(p_scope + k_scope_class_count);
        const uintptr_t class_table = R().ReadMem<uintptr_t>(p_scope + k_scope_class_table);

        const std::string enum_suffix = scope_name.substr(0, scope_name.find('.'));

        f << "// ── Scope: " << scope_name << " (" << class_count << " classes) ──\n";
        f << "enum ECS2Classes_" << enum_suffix << "\n{\n";

        auto write_class_line = [&](uintptr_t p_class)
        {
            if (!is_valid_ptr(p_class)) return;
            const uintptr_t p_name = R().ReadMem<uintptr_t>(p_class + k_class_name_ptr);
            if (!is_valid_ptr(p_name)) return;
            const std::string name = remote_str(p_name);
            if (name.empty()) return;

            const int32_t  sz    = R().ReadMem<int32_t>(p_class + k_class_sizeof);
            const int16_t  nf    = R().ReadMem<int16_t>(p_class + k_class_field_size);

            f << "    " << std::left << std::setw(60) << name
              << " // size=0x" << std::hex << sz << std::dec
              << "  fields=" << nf << '\n';
        };

        if (class_count > 0 && is_valid_ptr(class_table))
        {
            for (uint16_t j = 0; j < class_count; ++j)
            {
                const uintptr_t entry_base = class_table
                    + static_cast<uintptr_t>(j) * k_class_entry_stride;
                const uintptr_t p_binding  = R().ReadMem<uintptr_t>(entry_base + k_class_entry_ptr_off);
                if (!is_valid_ptr(p_binding)) continue;
                const uintptr_t p_class    = R().ReadMem<uintptr_t>(p_binding + k_binding_class_ptr);
                write_class_line(p_class);
            }
        }
        else
        {
            const auto scope_it = m_classes.find(scope_name);
            if (scope_it != m_classes.end())
            {
                for (const auto& [class_name, sc] : scope_it->second)
                    write_class_line(sc.p_remote);
            }
        }

        f << "};\n\n";
    }

    f.close();
    printf("[+] DumpAllClasses -> %s\n", path.c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
// DumpAllClassFields
// ─────────────────────────────────────────────────────────────────────────────
void CSchemaManager::DumpAllClassFields(const std::string& path) const
{
    std::ofstream f(path);
    if (!f.is_open()) { perror("[-] DumpAllClassFields: failed to open file"); return; }

    f << make_timestamp_header();
    f << "// CS2 Schema full field dump (all scopes, absolute offsets)\n\n";

    std::vector<std::pair<std::string, uintptr_t>> scopes;
    if (!collect_scopes(scopes)) { f << "// ERROR: failed to collect scopes.\n"; return; }

    for (const auto& [scope_name, p_scope] : scopes)
    {
        f << "// ════════════════════════════════════════════════════════════\n";
        f << "// Scope: " << scope_name << '\n';
        f << "// ════════════════════════════════════════════════════════════\n\n";

        const uint16_t  class_count = R().ReadMem<uint16_t>(p_scope + k_scope_class_count);
        const uintptr_t class_table = R().ReadMem<uintptr_t>(p_scope + k_scope_class_table);

        auto emit_class = [&](uintptr_t p_class)
        {
            if (!is_valid_ptr(p_class)) return;
            const uintptr_t p_name_ptr = R().ReadMem<uintptr_t>(p_class + k_class_name_ptr);
            if (!is_valid_str_ptr(p_name_ptr)) return;
            const std::string class_name = remote_str(p_name_ptr);
            if (class_name.empty()) return;
            const auto scope_it = m_classes.find(scope_name);
            if (scope_it == m_classes.end()) return;
            const auto class_it = scope_it->second.find(class_name);
            if (class_it == scope_it->second.end()) return;
            write_class_block(f, class_it->second);
        };

        if (class_count > 0 && is_valid_ptr(class_table))
        {
            for (uint16_t j = 0; j < class_count; ++j)
            {
                const uintptr_t entry_base = class_table
                    + static_cast<uintptr_t>(j) * k_class_entry_stride;
                const uintptr_t p_binding  = R().ReadMem<uintptr_t>(entry_base + k_class_entry_ptr_off);
                if (!is_valid_ptr(p_binding)) continue;
                const uintptr_t p_class    = R().ReadMem<uintptr_t>(p_binding + k_binding_class_ptr);
                emit_class(p_class);
            }
        }
        else
        {
            f << "// (no class table — using registered hash data)\n";
            const auto scope_it = m_classes.find(scope_name);
            if (scope_it != m_classes.end())
            {
                for (const auto& [name, sc] : scope_it->second)
                    write_class_block(f, sc);
            }
        }
    }

    f.close();
    printf("[+] DumpAllClassFields -> %s\n", path.c_str());
}