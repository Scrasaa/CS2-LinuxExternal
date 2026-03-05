#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  dmx_parser.h
//
//  Parses Valve's binary DMX format (encoding=binary_proto2, version 9)
//  as extracted from CS2 world_physics.vmdl_c by Source2Viewer-CLI.
//
//  Direct port of the Rust parser in avitran0/deadlocked.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace dmx
{

// ─────────────────────────────────────────────
//  Attribute value types  (mirrors Rust enum)
// ─────────────────────────────────────────────

struct Attribute;

using AttrElement       = std::optional<int32_t>;
using AttrElementArray  = std::vector<std::optional<int32_t>>;
using AttrIntArray      = std::vector<int32_t>;
using AttrFloatArray    = std::vector<float>;
using AttrBoolArray     = std::vector<bool>;
using AttrStringArray   = std::vector<std::string>;
using AttrVec2Array     = std::vector<glm::vec2>;
using AttrVec3Array     = std::vector<glm::vec3>;
using AttrVec4Array     = std::vector<glm::vec4>;
using AttrQuatArray     = std::vector<glm::quat>;
using AttrMat4Array     = std::vector<glm::mat4>;
using AttrU64Array      = std::vector<uint64_t>;
using AttrI32Array      = std::vector<int32_t>;
using AttrByteArray     = std::vector<uint8_t>;

using AttributeValue = std::variant<
    AttrElement,        // 1
    int32_t,            // 2
    float,              // 3
    bool,               // 4
    std::string,        // 5
    AttrByteArray,      // 6 / 47
    int32_t,            // 7  timespan
    uint32_t,           // 8  color
    glm::vec2,          // 9
    glm::vec3,          // 10 / 11 angle
    glm::vec4,          // 12
    glm::quat,          // 13
    glm::mat4,          // 14
    uint8_t,            // 15
    uint64_t,           // 16
    AttrElementArray,   // 33
    AttrIntArray,       // 34
    AttrFloatArray,     // 35
    AttrBoolArray,      // 36
    AttrStringArray,    // 37
    AttrVec2Array,      // 41
    AttrVec3Array,      // 42
    AttrVec4Array,      // 44
    AttrQuatArray,      // 45
    AttrMat4Array,      // 46
    AttrU64Array        // 48
>;

// ─────────────────────────────────────────────
//  Element
// ─────────────────────────────────────────────

struct Element
{
    std::string kind;
    std::string name;
    std::unordered_map<std::string, AttributeValue> attributes;

    void add(std::string sz_name, AttributeValue val)
    {
        attributes.emplace(std::move(sz_name), std::move(val));
    }

    const AttributeValue* get(const std::string& sz_name) const
    {
        const auto it = attributes.find(sz_name);
        return (it != attributes.end()) ? &it->second : nullptr;
    }
};

// ─────────────────────────────────────────────
//  Reader helpers
// ─────────────────────────────────────────────

class Reader
{
public:
    Reader(const uint8_t* p_data, std::size_t n_size)
        : m_p_data(p_data), m_n_size(n_size), m_n_pos(0u) {}

    template<typename T>
    T read()
    {
        if (m_n_pos + sizeof(T) > m_n_size)
            throw std::runtime_error("DMX: unexpected end of data");
        T val;
        std::memcpy(&val, m_p_data + m_n_pos, sizeof(T));
        m_n_pos += sizeof(T);
        return val;
    }

    std::string read_string()
    {
        std::string s;
        while (m_n_pos < m_n_size)
        {
            const char c = static_cast<char>(m_p_data[m_n_pos++]);
            if (c == '\0') break;
            s.push_back(c);
        }
        return s;
    }

    std::vector<uint8_t> read_bytes(std::size_t n_count)
    {
        if (m_n_pos + n_count > m_n_size)
            throw std::runtime_error("DMX: unexpected end of data");
        std::vector<uint8_t> buf(m_p_data + m_n_pos, m_p_data + m_n_pos + n_count);
        m_n_pos += n_count;
        return buf;
    }

    glm::vec2 read_vec2() { return { read<float>(), read<float>() }; }
    glm::vec3 read_vec3() { return { read<float>(), read<float>(), read<float>() }; }
    glm::vec4 read_vec4() { return { read<float>(), read<float>(), read<float>(), read<float>() }; }
    glm::quat read_quat() { float x=read<float>(),y=read<float>(),z=read<float>(),w=read<float>(); return {w,x,y,z}; }
    glm::mat4 read_mat4()
    {
        glm::mat4 m;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                m[i][j] = read<float>();
        return m;
    }

private:
    const uint8_t* m_p_data;
    std::size_t    m_n_size;
    std::size_t    m_n_pos;
};

// ─────────────────────────────────────────────
//  Parse
// ─────────────────────────────────────────────

inline std::unordered_map<std::string, Element> parse(
    const uint8_t* p_data, std::size_t n_size)
{
    Reader r(p_data, n_size);

    // Header: null-terminated string
    r.read_string();

    // prefix element count (unused)
    r.read<int32_t>();

    // String table
    const int32_t n_string_count = r.read<int32_t>();
    std::vector<std::string> a_strings;
    a_strings.reserve(static_cast<std::size_t>(n_string_count));
    for (int32_t i = 0; i < n_string_count; ++i)
        a_strings.push_back(r.read_string());

    // Element stubs
    const int32_t n_elem_count = r.read<int32_t>();
    std::vector<Element> a_elements;
    a_elements.reserve(static_cast<std::size_t>(n_elem_count));
    for (int32_t i = 0; i < n_elem_count; ++i)
    {
        Element e;
        e.kind = a_strings[static_cast<std::size_t>(r.read<int32_t>())];
        e.name = a_strings[static_cast<std::size_t>(r.read<int32_t>())];
        r.read_bytes(16); // uuid
        a_elements.push_back(std::move(e));
    }

    // Attribute data
    for (auto& elem : a_elements)
    {
        const int32_t n_attr_count = r.read<int32_t>();
        for (int32_t i = 0; i < n_attr_count; ++i)
        {
            const std::string sz_name = a_strings[static_cast<std::size_t>(r.read<int32_t>())];
            const uint8_t     n_kind  = r.read<uint8_t>();

            AttributeValue val;
            switch (n_kind)
            {
                case 1:
                {
                    const int32_t idx = r.read<int32_t>();
                    val = (idx == -1) ? AttrElement{std::nullopt} : AttrElement{idx};
                    break;
                }
                case 2:  val = r.read<uint32_t>();  break;
                case 3:  val = r.read<float>();    break;
                case 4:  val = r.read<uint8_t>() != 0u; break;
                case 5:  val = a_strings[static_cast<std::size_t>(r.read<int32_t>())]; break;
                case 6:  { const int32_t n = r.read<int32_t>(); val = r.read_bytes(static_cast<std::size_t>(n)); break; }
                case 7:  val = r.read<uint32_t>();  break;
                case 8:  val = r.read<uint32_t>(); break;
                case 9:  val = r.read_vec2(); break;
                case 10: val = r.read_vec3(); break;
                case 11: val = r.read_vec3(); break;
                case 12: val = r.read_vec4(); break;
                case 13: val = r.read_quat(); break;
                case 14: val = r.read_mat4(); break;
                case 15: val = r.read<uint8_t>(); break;
                case 16: val = r.read<uint64_t>(); break;

                case 33:
                {
                    const int32_t n = r.read<int32_t>();
                    AttrElementArray arr;
                    arr.reserve(static_cast<std::size_t>(n));
                    for (int32_t j = 0; j < n; ++j)
                    {
                        const int32_t idx = r.read<int32_t>();
                        arr.push_back(idx == -1 ? std::nullopt : std::optional<int32_t>{idx});
                    }
                    val = std::move(arr);
                    break;
                }
                case 34: { const int32_t n=r.read<int32_t>(); AttrIntArray a; a.reserve(n); for(int32_t j=0;j<n;++j) a.push_back(r.read<int32_t>()); val=std::move(a); break; }
                case 35: { const int32_t n=r.read<int32_t>(); AttrFloatArray a; a.reserve(n); for(int32_t j=0;j<n;++j) a.push_back(r.read<float>()); val=std::move(a); break; }
                case 36: { const int32_t n=r.read<int32_t>(); AttrBoolArray a; a.reserve(n); for(int32_t j=0;j<n;++j) a.push_back(r.read<uint8_t>()!=0u); val=std::move(a); break; }
                case 37: { const int32_t n=r.read<int32_t>(); AttrStringArray a; a.reserve(n); for(int32_t j=0;j<n;++j) a.push_back(r.read_string()); val=std::move(a); break; }
                case 39: { const int32_t n=r.read<int32_t>(); AttrIntArray a; a.reserve(n); for(int32_t j=0;j<n;++j) a.push_back(r.read<int32_t>()); val=std::move(a); break; }
                case 40: { const int32_t n=r.read<int32_t>(); AttrIntArray a; a.reserve(n); for(int32_t j=0;j<n;++j) a.push_back(static_cast<int32_t>(r.read<uint32_t>())); val=std::move(a); break; }
                case 41: { const int32_t n=r.read<int32_t>(); AttrVec2Array a; a.reserve(n); for(int32_t j=0;j<n;++j) a.push_back(r.read_vec2()); val=std::move(a); break; }
                case 42: { const int32_t n=r.read<int32_t>(); AttrVec3Array a; a.reserve(n); for(int32_t j=0;j<n;++j) a.push_back(r.read_vec3()); val=std::move(a); break; }
                case 43: { const int32_t n=r.read<int32_t>(); AttrVec3Array a; a.reserve(n); for(int32_t j=0;j<n;++j) a.push_back(r.read_vec3()); val=std::move(a); break; }
                case 44: { const int32_t n=r.read<int32_t>(); AttrVec4Array a; a.reserve(n); for(int32_t j=0;j<n;++j) a.push_back(r.read_vec4()); val=std::move(a); break; }
                case 45: { const int32_t n=r.read<int32_t>(); AttrQuatArray a; a.reserve(n); for(int32_t j=0;j<n;++j) a.push_back(r.read_quat()); val=std::move(a); break; }
                case 46: { const int32_t n=r.read<int32_t>(); AttrMat4Array a; a.reserve(n); for(int32_t j=0;j<n;++j) a.push_back(r.read_mat4()); val=std::move(a); break; }
                case 47: { const int32_t n=r.read<int32_t>(); val=r.read_bytes(static_cast<std::size_t>(n)); break; }
                case 48: { const int32_t n=r.read<int32_t>(); AttrU64Array a; a.reserve(n); for(int32_t j=0;j<n;++j) a.push_back(r.read<uint64_t>()); val=std::move(a); break; }

                default:
                    throw std::runtime_error("DMX: unknown attribute kind " + std::to_string(n_kind));
            }

            elem.add(sz_name, std::move(val));
        }
    }

    // Index by "kind_name"
    std::unordered_map<std::string, Element> result;
    for (auto& e : a_elements)
    {
        const std::string sz_key = e.kind + "_" + e.name;
        result.emplace(sz_key, std::move(e));
    }
    return result;
}

} // namespace dmx