//
// Created by scrasa on 08.02.26.
//

#include "Utils.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

// ---------- Static definitions ----------

CUtils::ModuleCache CUtils::g_moduleCache;
std::unique_ptr<CUtils> CUtils::s_instance = nullptr;

// ---------- Process discovery ----------

std::vector<pid_t> CUtils::FindPidsByName(const std::string& name)
{
    namespace fs = std::filesystem;
    std::vector<pid_t> result;
    result.reserve(8);

    for (const auto& entry : fs::directory_iterator("/proc"))
    {
        const std::string pid_str = entry.path().filename().string();

        if (pid_str.empty() || !std::isdigit(static_cast<unsigned char>(pid_str[0])))
            continue;

        std::ifstream comm_file(entry.path() / "comm");
        std::string   proc_name;
        if (!std::getline(comm_file, proc_name))
            continue;

        // 'comm' truncates to 15 chars; exact match is sufficient for CS2
        if (proc_name != name)
            continue;

        try
        {
            result.push_back(static_cast<pid_t>(std::stoi(pid_str)));
        }
        catch (...) { /* non-numeric or overflow */ }
    }

    return result;
}

// ---------- Module map parsing ----------

std::optional<CUtils::ModuleInfo> CUtils::GetModuleInfoFromMaps(pid_t pid, const std::string& moduleName)
{
    const std::string maps_path = "/proc/" + std::to_string(pid) + "/maps";
    std::ifstream     maps(maps_path);
    if (!maps.is_open())
        return std::nullopt;

    ModuleInfo info{};
    uintptr_t  region_end = 0;
    bool       found      = false;

    std::string line;
    while (std::getline(maps, line))
    {
        if (line.find(moduleName) == std::string::npos)
            continue;

        std::istringstream iss(line);
        std::string address_range, perms, offset, dev, inode, pathname;

        if (!(iss >> address_range >> perms >> offset >> dev >> inode))
            continue;

        std::getline(iss, pathname);

        // Trim leading whitespace
        const size_t path_start = pathname.find_first_not_of(" \t");
        if (path_start == std::string::npos)
            continue;
        pathname = pathname.substr(path_start);

        // Match exact filename
        const size_t last_slash = pathname.find_last_of('/');
        const std::string file_name = (last_slash != std::string::npos)
                                    ? pathname.substr(last_slash + 1)
                                    : pathname;
        if (file_name != moduleName)
            continue;

        // Parse "start-end" hex range
        const size_t dash = address_range.find('-');
        if (dash == std::string::npos)
            continue;

        const uintptr_t range_start = std::stoull(address_range.substr(0, dash),        nullptr, 16);
        const uintptr_t range_end   = std::stoull(address_range.substr(dash + 1), nullptr, 16);

        if (!found)
        {
            info.base = range_start;
            info.path = pathname;
            found     = true;
        }

        region_end = std::max(region_end, range_end);
    }

    if (!found)
        return std::nullopt;

    info.size = region_end - info.base;
    return info;
}

std::optional<CUtils::ModuleInfo> CUtils::GetModuleInfo(pid_t pid, const std::string& moduleName, bool useCache)
{
    if (moduleName.empty())
        return std::nullopt;

    if (useCache)
    {
        auto cached = g_moduleCache.Get(moduleName);
        if (cached.has_value())
            return cached;
    }

    auto info = GetModuleInfoFromMaps(pid, moduleName);

    if (info.has_value() && useCache)
        g_moduleCache.Set(moduleName, *info);

    return info;
}

// ---------- Pattern scanning ----------

// Internal helper: parse IDA pattern string into bytes + mask
static void parse_ida_pattern(const std::string& idaPattern,
                               std::vector<uint8_t>& out_pattern,
                               std::string& out_mask)
{
    std::istringstream iss(idaPattern);
    std::string        byte_str;

    while (iss >> byte_str)
    {
        if (byte_str == "?" || byte_str == "??")
        {
            out_pattern.push_back(0);
            out_mask += '?';
        }
        else
        {
            out_pattern.push_back(static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16)));
            out_mask += 'x';
        }
    }
}

// Internal scan core — operates on a flat byte buffer
static bool match_pattern(const uint8_t* data,
                           const std::vector<uint8_t>& pattern,
                           const std::string& mask)
{
    for (size_t j = 0; j < pattern.size(); ++j)
    {
        if (mask[j] == 'x' && data[j] != pattern[j])
            return false;
    }
    return true;
}

uintptr_t CUtils::PatternScan(const std::string& moduleName,
                               const std::vector<uint8_t>& pattern,
                               const std::string& mask) const
{
    if (pattern.size() != mask.size() || pattern.empty())
        return 0;

    const auto info = GetModuleInfo(m_pid, moduleName);
    if (!info.has_value() || info->size == 0)
        return 0;

    const size_t pattern_len = pattern.size();
    constexpr size_t k_chunk = 1024 * 1024; // 1 MB, maximum is 10MB defined in our kdriver

    // Overlap consecutive chunks by (patternLen - 1) so cross-boundary matches aren't missed
    const size_t overlap = pattern_len > 1 ? pattern_len - 1 : 0;

    std::vector<uint8_t> buffer(k_chunk);

    for (size_t offset = 0; offset < info->size; )
    {
        const size_t bytes_to_read = std::min(k_chunk, info->size - offset);
        if (bytes_to_read < pattern_len)
            break;

        if (!ReadRaw(info->base + offset, buffer.data(), bytes_to_read))
        {
            offset += bytes_to_read;
            continue;
        }

        const size_t scan_limit = bytes_to_read - pattern_len + 1;
        for (size_t i = 0; i < scan_limit; ++i)
        {
            if (match_pattern(buffer.data() + i, pattern, mask))
                return info->base + offset + i;
        }

        // Advance, but keep the tail so patterns spanning chunks are found
        offset += bytes_to_read - overlap;
    }

    return 0;
}

uintptr_t CUtils::PatternScan(const std::string& moduleName, const std::string& idaPattern) const
{
    std::vector<uint8_t> pattern;
    std::string          mask;
    parse_ida_pattern(idaPattern, pattern, mask);
    return PatternScan(moduleName, pattern, mask);
}

std::vector<uintptr_t> CUtils::PatternScanAll(const std::string& moduleName,
                                               const std::vector<uint8_t>& pattern,
                                               const std::string& mask) const
{
    std::vector<uintptr_t> results;

    if (pattern.size() != mask.size() || pattern.empty())
        return results;

    const auto info = GetModuleInfo(m_pid, moduleName);
    if (!info.has_value() || info->size == 0)
        return results;

    const size_t pattern_len = pattern.size();
    constexpr size_t k_chunk = 1024 * 1024;
    const size_t     overlap = pattern_len > 1 ? pattern_len - 1 : 0;

    std::vector<uint8_t> buffer(k_chunk);

    for (size_t offset = 0; offset < info->size; )
    {
        const size_t bytes_to_read = std::min(k_chunk, info->size - offset);
        if (bytes_to_read < pattern_len)
            break;

        if (!ReadRaw(info->base + offset, buffer.data(), bytes_to_read))
        {
            offset += bytes_to_read;
            continue;
        }

        const size_t scan_limit = bytes_to_read - pattern_len + 1;
        for (size_t i = 0; i < scan_limit; ++i)
        {
            if (match_pattern(buffer.data() + i, pattern, mask))
                results.push_back(info->base + offset + i);
        }

        offset += bytes_to_read - overlap;
    }

    return results;
}

// ---------- Source2 interface resolution ----------
// ---------- ELF helpers ----------

[[nodiscard]] uintptr_t CUtils::GetSegmentFromPHT(uintptr_t base_address, uint32_t tag) const
{
    if (!base_address)
        return 0;

    // Elf64_Ehdr offsets
    const uintptr_t first_entry = ReadMem<uint64_t>(base_address + 0x20) + base_address; // e_phoff
    const uint64_t  entry_size  = ReadMem<uint16_t>(base_address + 0x36);                // e_phentsize
    const auto  num_entries = ReadMem<uint16_t>(base_address + 0x38);                // e_phnum

    for (uint16_t i = 0; i < num_entries; ++i)
    {
        const uintptr_t entry = first_entry + i * entry_size;
        if (ReadMem<uint32_t>(entry) == tag) // p_type
            return entry;
    }

    return 0;
}

[[nodiscard]] uintptr_t CUtils::GetAddressFromDynamicSection(uintptr_t base_address, uint64_t tag) const
{
    constexpr uint64_t k_register_size = 8;

    const uintptr_t dynamic_section = GetSegmentFromPHT(base_address, 2); // PT_DYNAMIC = 2
    if (!dynamic_section)
        return 0;

    // p_offset is at +0x08 in Elf64_Phdr; add base to get loaded VA
    uintptr_t address = ReadMem<uint64_t>(dynamic_section + 2 * k_register_size) + base_address;

    while (true)
    {
        const auto tag_value = ReadMem<uint64_t>(address);
        if (tag_value == 0) // DT_NULL
            break;

        if (tag_value == tag)
            return ReadMem<uint64_t>(address + k_register_size);

        address += k_register_size * 2;
    }

    return 0;
}

[[nodiscard]] uintptr_t CUtils::GetModuleExport(uintptr_t base_address, std::string_view export_name) const
{
    constexpr uintptr_t k_sym_entry_size = 0x18; // sizeof(Elf64_Sym)

    const uintptr_t string_table = GetAddressFromDynamicSection(base_address, 0x05); // DT_STRTAB
    uintptr_t       symbol_table = GetAddressFromDynamicSection(base_address, 0x06); // DT_SYMTAB

    if (!string_table || !symbol_table)
        return 0;

    symbol_table += k_sym_entry_size; // skip null symbol entry [0]

    while (true)
    {
        const auto st_name = ReadMem<uint32_t>(symbol_table);
        if (st_name == 0)
            break;

        const std::string name = ReadString(string_table + st_name);
        if (name == export_name)
            return ReadMem<uint64_t>(symbol_table + 0x08) + base_address; // st_value + base

        symbol_table += k_sym_entry_size;
    }

    return 0;
}

[[nodiscard]] std::optional<uintptr_t> CUtils::GetInterfaceOffset(
    uintptr_t base_address,
    std::string_view interface_name) const
{
    // Resolve CreateInterface export
    const uintptr_t export_fn = GetModuleExport(base_address, "CreateInterface");
    if (!export_fn)
        return std::nullopt;

    // Follow RIP-relative mov to reach the InterfaceReg linked list head
    // Stub layout: ... mov rax, [rip + rel32] ...
    //              bytes at +0x03 = rel32, instruction ends at +0x07
    const uintptr_t export_address   = export_fn + 0x10;
    const uintptr_t p_interface_list = ResolveRelativeAddress(export_address, 0x03, 0x07);
    auto            p_entry          = ReadMem<uintptr_t>(p_interface_list);

    while (is_valid_ptr(p_entry))
    {
        // InterfaceReg layout (Source2):
        //   +0x00  uintptr_t    m_pfnCreate   — creator stub ptr
        //   +0x08  const char*  m_pszName     — interface name
        //   +0x10  InterfaceReg* m_pNext      — linked list
        const auto ppsz_name  = ReadMem<uintptr_t>(p_entry + 0x08);
        const auto      entry_name = ReadString(ppsz_name);

        if (entry_name.starts_with(interface_name))
        {
            const auto vfunc_address = ReadMem<uintptr_t>(p_entry);
            return ResolveRelativeAddress(vfunc_address, 0x03, 0x07);
        }

        p_entry = ReadMem<uintptr_t>(p_entry + 0x10);
    }

    return std::nullopt;
}