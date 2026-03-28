#include "map_manager.h"

#include <cstdio>
#include <fstream>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "map_parser.h"

namespace fs = std::filesystem;

// ─────────────────────────────────────────────
//  Internal helper — find CS2 maps directory
//
//  Result is cached after the first successful
//  resolve so repeated calls are free.
//
//  When running as root via sudo, $HOME is /root
//  but the actual user's Steam is under /home/<user>.
//  SUDO_USER contains the real username in that case.
// ─────────────────────────────────────────────

static fs::path find_maps_dir()
{
    static fs::path s_cached   = {};
    static bool     s_resolved = false;

    if (s_resolved)
        return s_cached;

    s_resolved = true;

    // Build a list of home directories to try
    std::vector<fs::path> a_homes;

    const char* psz_sudo_user = std::getenv("SUDO_USER");
    if (psz_sudo_user && psz_sudo_user[0] != '\0')
        a_homes.push_back(fs::path("/home") / psz_sudo_user);

    const char* psz_home = std::getenv("HOME");
    if (psz_home && psz_home[0] != '\0')
        a_homes.push_back(fs::path(psz_home));

    // Also scan all users in /home as a fallback
    if (fs::exists("/home"))
    {
        for (const fs::directory_entry& entry : fs::directory_iterator("/home"))
        {
            if (entry.is_directory())
                a_homes.push_back(entry.path());
        }
    }

    static constexpr const char* k_cs2_maps =
        "Counter-Strike Global Offensive/game/csgo/maps";

    for (const fs::path& home : a_homes)
    {
        const fs::path a_candidates[] =
        {
            home / ".local/share/Steam/steamapps/common" / k_cs2_maps,
            home / ".steam/steam/steamapps/common"       / k_cs2_maps,
            home / ".steam/debian-installation/steamapps/common" / k_cs2_maps,
        };

        for (const fs::path& p : a_candidates)
        {
            if (fs::exists(p))
            {
                s_cached = p;
                printf("[map] maps dir: '%s'\n", p.string().c_str());
                return s_cached;
            }
        }
    }

    return {};
}

// ─────────────────────────────────────────────
//  Constructor
// ─────────────────────────────────────────────

MapManager::MapManager(
    uintptr_t                                           p_global_vars,
    std::function<uintptr_t(uintptr_t)>                fn_read_ptr,
    std::function<std::string(uintptr_t, std::size_t)> fn_read_string,
    std::string                                         sz_s2v_binary)
    : m_p_global_vars  (p_global_vars)
    , m_fn_read_ptr    (std::move(fn_read_ptr))
    , m_fn_read_string (std::move(fn_read_string))
    , m_sz_s2v_binary  (std::move(sz_s2v_binary))
{}

// ─────────────────────────────────────────────
//  Public
// ─────────────────────────────────────────────

void MapManager::update()
{
    const std::string sz_map = read_map_name();
    if (sz_map.empty() || (sz_map == m_sz_current_map && m_b_loaded))
        return;

    m_sz_current_map = sz_map;
    m_bvh            = Bvh{};
    m_b_loaded       = false;

    const fs::path path_geo = find_geo_path(sz_map);
    const fs::path path_vpk = find_vpk_path(sz_map);
    const fs::path path_bvh = [&]
    {
        fs::path p = path_geo;
        p.replace_extension(".bvh");
        return p;
    }();

    // ── Fast path: pre-built BVH cache ───────────────────────────────────
    if (fs::exists(path_bvh))
    {
        // Invalidate if VPK is newer (CS2 updated)
        const bool b_vpk_newer = fs::exists(path_vpk)
            && fs::last_write_time(path_vpk) > fs::last_write_time(path_bvh);

        if (!b_vpk_newer)
        {
            m_b_loaded = load_bvh_cache(path_bvh);
            if (m_b_loaded)
                return;
            // Cache corrupt — fall through to rebuild
        }
        else
        {
            printf("[map] VPK newer than .bvh cache — rebuilding\n");
            fs::remove(path_bvh);
            fs::remove(path_geo); // geo is also stale
        }
    }

    // ── Medium path: raw .geo exists, build BVH then cache it ────────────
    if (fs::exists(path_geo))
    {
        if (fs::exists(path_vpk) && fs::last_write_time(path_vpk) > fs::last_write_time(path_geo))
        {
            printf("[map] VPK newer than .geo — reparsing\n");
            fs::remove(path_geo);
        }
        else
        {
            m_b_loaded = load_geo(path_geo);
            if (m_b_loaded)
            {
                if (save_bvh_cache(path_bvh)) // non-fatal if it fails
                    printf("[map] built and cached BVH: '%s'\n", path_bvh.string().c_str());
            }
            return;
        }
    }

    // ── Slow path: parse VPK → .geo → build → cache ──────────────────────
    if (path_vpk.empty() || !fs::exists(path_vpk))
        return;

    if (!parse_map_to_geo(m_sz_s2v_binary, path_vpk, path_geo))
        return;

    printf("[map] parsed vpk → '%s'\n", path_geo.string().c_str());

    m_b_loaded = load_geo(path_geo);
    if (m_b_loaded)
    {
        if (save_bvh_cache(path_bvh))
            printf("[map] built and cached BVH: '%s'\n", path_bvh.string().c_str());
    }
}

bool MapManager::is_visible(const glm::vec3& v3_start, const glm::vec3& v3_end) const
{
    if (!m_b_loaded)
        return true;

    return m_bvh.has_line_of_sight(v3_start, v3_end);
}

// ─────────────────────────────────────────────
//  Private
// ─────────────────────────────────────────────

std::string MapManager::read_map_name() const
{
    const uintptr_t p_base     = m_fn_read_ptr(m_p_global_vars);
    const uintptr_t p_name_str = m_fn_read_ptr(p_base + 0x198);
    if (!p_base || !p_name_str)
        return {};

    std::string sz_name = m_fn_read_string(p_name_str, 64u);

    // Strip "maps/" prefix CS2 sometimes includes
    const auto n_slash = sz_name.rfind('/');
    if (n_slash != std::string::npos)
        sz_name = sz_name.substr(n_slash + 1);

    // Strip .vpk extension if present
    if (sz_name.size() > 4u && sz_name.substr(sz_name.size() - 4u) == ".vpk")
        sz_name = sz_name.substr(0u, sz_name.size() - 4u);

    // Ignore empty or menu states
    if (sz_name.empty() || sz_name == "<empty>" || sz_name == "menu" || sz_name == "lobby")
        return {};

    return sz_name;
}

fs::path MapManager::find_geo_path(const std::string& sz_map_name)
{
    const fs::path path_maps = find_maps_dir();
    if (path_maps.empty())
        return {};
    return path_maps / (sz_map_name + ".geo");
}

fs::path MapManager::find_vpk_path(const std::string& sz_map_name)
{
    const fs::path path_maps = find_maps_dir();
    if (path_maps.empty())
        return {};
    return path_maps / (sz_map_name + ".vpk");
}


template<typename T>
struct uninit_alloc : public std::allocator<T>
{
    using Base = std::allocator<T>;

    template<typename U>
    struct rebind { using other = uninit_alloc<U>; };

    // Suppress default-construction — leaves memory uninitialised
    void construct(T*) noexcept {}

    // Forward all other construction normally (emplace, resize-with-value, etc.)
    template<typename... Args>
    void construct(T* p_obj, Args&&... args)
    {
        Base::construct(p_obj, std::forward<Args>(args)...);
    }
};

template<typename T>
[[nodiscard]] std::vector<T> make_uninitialized_vector(const size_t n_count)
{
    static_assert(std::is_trivially_copyable_v<T>,
        "make_uninitialized_vector is only safe for trivially copyable types");

    // Construct with uninit allocator → no zero-init pass
    std::vector<T, uninit_alloc<T>> v_raw(n_count);

    // Move the raw storage into a standard vector via the iterator range ctor.
    // The compiler will reduce this to a single memcpy since T is trivial.
    return std::vector<T>(
        std::make_move_iterator(v_raw.begin()),
        std::make_move_iterator(v_raw.end())
    );
}

struct BVH_Header
{
    uint64_t n_magic;
    uint64_t n_version;
    uint64_t n_node_count;
    uint64_t n_tri_count;
    uint64_t n_prim_idx_count;
};

constexpr uint64_t k_bvh_magic   = 0x4856425F50414D00ull;
constexpr uint64_t k_bvh_version = 2ull;

bool MapManager::save_bvh_cache(const fs::path& path_cache) const
{
    const auto& v_nodes       = m_bvh.nodes();
    const auto& v_prim_indices = m_bvh.prim_indices();
    const auto& v_tris        = m_bvh.tris();

    const int n_fd = ::open(path_cache.c_str(),
        O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (n_fd < 0)
        return false;

    const BVH_Header hdr
    {
        .n_magic          = k_bvh_magic,
        .n_version        = k_bvh_version,
        .n_node_count     = v_nodes.size(),
        .n_tri_count      = v_tris.size(),
        .n_prim_idx_count = v_prim_indices.size(),
    };

    ::write(n_fd, &hdr,                sizeof(hdr));
    ::write(n_fd, v_nodes.data(),       v_nodes.size()        * sizeof(v_nodes[0]));
    ::write(n_fd, v_prim_indices.data(), v_prim_indices.size() * sizeof(v_prim_indices[0]));
    ::write(n_fd, v_tris.data(),        v_tris.size()         * sizeof(v_tris[0]));
    ::close(n_fd);
    return true;
}

bool MapManager::load_bvh_cache(const fs::path& path_cache)
{
    static_assert(std::is_trivially_copyable_v<BvhNode>);
    static_assert(std::is_trivially_copyable_v<Triangle>);

    const int n_fd = ::open(path_cache.c_str(), O_RDONLY);
    if (n_fd < 0)
        return false;

    ::posix_fadvise(n_fd, 0, 0, POSIX_FADV_SEQUENTIAL | POSIX_FADV_WILLNEED);

    struct stat st{};
    ::fstat(n_fd, &st);
    const size_t n_file_size = static_cast<size_t>(st.st_size);

    void* const p_map = ::mmap(nullptr, n_file_size,
        PROT_READ, MAP_PRIVATE | MAP_POPULATE, n_fd, 0);
    ::close(n_fd);

    if (p_map == MAP_FAILED)
        return false;

    auto fn_cleanup = [&]() noexcept { ::munmap(p_map, n_file_size); };

    const auto* pb = static_cast<const uint8_t*>(p_map);

    if (n_file_size < sizeof(BVH_Header)) { fn_cleanup(); return false; }

    BVH_Header hdr;
    std::memcpy(&hdr, pb, sizeof(hdr));
    pb += sizeof(hdr);

    if (hdr.n_magic != k_bvh_magic || hdr.n_version != k_bvh_version)
    {
        fn_cleanup();
        return false;
    }

    const size_t n_node_bytes     = hdr.n_node_count     * sizeof(BvhNode);
    const size_t n_prim_idx_bytes = hdr.n_prim_idx_count * sizeof(uint32_t);
    const size_t n_tri_bytes      = hdr.n_tri_count      * sizeof(Triangle);

    if (n_file_size < sizeof(hdr) + n_node_bytes + n_prim_idx_bytes + n_tri_bytes)
    {
        fn_cleanup();
        return false;
    }

    m_bvh.nodes().resize(hdr.n_node_count);
    m_bvh.prim_indices().resize(hdr.n_prim_idx_count);
    m_bvh.tris().resize(hdr.n_tri_count);

    std::memcpy(m_bvh.nodes().data(),        pb,                                    n_node_bytes);
    std::memcpy(m_bvh.prim_indices().data(), pb + n_node_bytes,                    n_prim_idx_bytes);
    std::memcpy(m_bvh.tris().data(),         pb + n_node_bytes + n_prim_idx_bytes, n_tri_bytes);

    fn_cleanup();
    printf("[map] BVH cache loaded: %llu nodes, %llu prim indices, %llu tris\n",
           static_cast<unsigned long long>(hdr.n_node_count),
           static_cast<unsigned long long>(hdr.n_prim_idx_count),
           static_cast<unsigned long long>(hdr.n_tri_count));
    return true;
}

// Should be faster than before?
bool MapManager::load_geo(const fs::path& path_geo)
{
    // ── Guarantee blittability once at compile time ───────────────────────
    static_assert(sizeof(Triangle)              == 9u * sizeof(float),
        "Triangle layout must be exactly 9 packed floats");
    static_assert(std::is_trivially_copyable_v<Triangle>,
        "Triangle must be trivially copyable for memcpy path");

    // ── Open + advise before mmap so the kernel starts async readahead ────
    const int n_fd = ::open(path_geo.c_str(), O_RDONLY);
    if (n_fd < 0)
        return false;

    // Triggers async read-ahead in the kernel *before* we even mmap
    ::posix_fadvise(n_fd, 0, 0, POSIX_FADV_SEQUENTIAL | POSIX_FADV_WILLNEED);

    struct stat st {};
    if (::fstat(n_fd, &st) < 0) { ::close(n_fd); return false; }

    const size_t n_file_size = static_cast<size_t>(st.st_size);
    if (n_file_size < sizeof(uint64_t)) { ::close(n_fd); return false; }

    // MAP_POPULATE: pre-faults ALL pages into memory during mmap() itself.
    // The call blocks until I/O is done → zero page-fault stalls in the hot path.
    // Pair with posix_fadvise above so the kernel already has the data warm.
    void* const p_map = ::mmap(
        nullptr, n_file_size,
        PROT_READ,
        MAP_PRIVATE | MAP_POPULATE,
        n_fd, 0
    );
    ::close(n_fd); // fd no longer needed after mmap

    if (p_map == MAP_FAILED)
        return false;

    // Let TLB/prefetch hardware know access pattern is linear
    ::madvise(p_map, n_file_size, MADV_SEQUENTIAL);

    auto fn_cleanup = [&]() noexcept { ::munmap(p_map, n_file_size); };

    // ── Parse header ──────────────────────────────────────────────────────
    const auto*    pb          = static_cast<const uint8_t*>(p_map);
    const uint64_t n_tri_count = *reinterpret_cast<const uint64_t*>(pb);
    pb += sizeof(uint64_t);

    constexpr uint64_t k_max_triangles = 50'000'000ull;
    constexpr size_t   k_bytes_per_tri = 9u * sizeof(float);

    const size_t n_data_needed = n_tri_count * k_bytes_per_tri;
    const size_t n_data_avail  = n_file_size - sizeof(uint64_t);

    if (n_tri_count == 0u
        || n_tri_count > k_max_triangles
        || n_data_avail < n_data_needed)
    {
        fn_cleanup();
        return false;
    }

    printf("[map] loading %llu triangles\n",
           static_cast<unsigned long long>(n_tri_count));


    const auto* p_tris = reinterpret_cast<const Triangle*>(pb);
    m_bvh.build(std::span<const Triangle>{ p_tris, n_tri_count });
    fn_cleanup();

    return true;
}