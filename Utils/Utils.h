//
// Created by scrasa on 08.02.26.
//

#ifndef CS2_LINUXEXTERNAL_UTILS_H
#define CS2_LINUXEXTERNAL_UTILS_H

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <stdexcept>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <linux/input.h>
#include <atomic>
#include <thread>
#include <dirent.h>
#include <random>

#include "imgui.h"
#include "../SDK/defs.h"

////////////////////////
struct mem_request
{
    pid_t         pid;
    unsigned long addr;
    unsigned long size;
    void*         buffer;
};


struct phys_query
{
    pid_t         pid;
    unsigned long virt_addr;   // [in]
    unsigned long phys_addr;   // [out]
};

#define IOCTL_READ_MEM  _IOR('k', 1, struct mem_request*)
#define IOCTL_WRITE_MEM _IOW('k', 2, struct mem_request*)
#define IOCTL_VIRT_TO_PHYS   _IOWR('k', 3, struct phys_query*)
#define IOCTL_READ_PHYS_MEM  _IOR('k',  4, struct mem_request*)

class CUtils
{
public:
    const int   m_fd{};
    const pid_t m_pid{};

    CUtils(int fd, pid_t pid) : m_fd(fd), m_pid(pid) {}

    ~CUtils()
    {
        if (m_fd > 0)
            close(m_fd);
    }

    // Disable copy; ownership is unique
    CUtils(const CUtils&)            = delete;
    CUtils& operator=(const CUtils&) = delete;

    // ---------- Core I/O ----------

    /*
    [[nodiscard]] bool ReadRaw(uintptr_t address, void* buffer, size_t size) const
    {
        if (!address || !buffer || size == 0)
            return false;

        mem_request req{};
        req.pid    = m_pid;
        req.addr   = static_cast<unsigned long>(address);
        req.size   = size;
        req.buffer = buffer;

        return ioctl(m_fd, IOCTL_READ_MEM, &req) >= 0;
    }
    */

    [[nodiscard]] bool ReadRaw(uintptr_t address, void* buffer, size_t size) const
    {
        if (!address || !buffer || size == 0)
            return false;

        mem_request req{};
        req.pid    = m_pid;
        req.addr   = static_cast<unsigned long>(address);
        req.size   = size;
        req.buffer = buffer;

        return ioctl(m_fd, IOCTL_READ_PHYS_MEM, &req) >= 0;
    }

    [[nodiscard]] bool WriteRaw(uintptr_t address, const void* buffer, size_t size) const
    {
        if (!address || !buffer || size == 0)
            return false;

        // ioctl buffer must be non-const; copy to local staging buffer
        std::vector<uint8_t> staging(size);
        std::memcpy(staging.data(), buffer, size);

        mem_request req{};
        req.pid    = m_pid;
        req.addr   = static_cast<unsigned long>(address);
        req.size   = size;
        req.buffer = staging.data();

        return ioctl(m_fd, IOCTL_WRITE_MEM, &req) >= 0;
    }

    // Physical-backed read — pins pages, bypasses VMA permission checks.
    // Use when ReadRaw fails on valid-but-restricted mappings,
    // or when you want to verify the read is hitting real physical frames.
    [[nodiscard]] bool ReadPhysical(uintptr_t address, void* buffer, size_t size) const
    {
        if (!address || !buffer || size == 0)
            return false;

        mem_request req{};
        req.pid    = m_pid;
        req.addr   = static_cast<unsigned long>(address);
        req.size   = size;
        req.buffer = buffer;

        return ioctl(m_fd, IOCTL_READ_PHYS_MEM, &req) >= 0;
    }

    // Translate a virtual address in the target process to its physical address.
    // Returns 0 on failure.
    [[nodiscard]] uintptr_t VirtToPhys(uintptr_t address) const
    {
        if (!address)
            return 0;

        phys_query pq{};
        pq.pid       = m_pid;
        pq.virt_addr = static_cast<unsigned long>(address);
        pq.phys_addr = 0;

        if (ioctl(m_fd, IOCTL_VIRT_TO_PHYS, &pq) < 0)
            return 0;

        return static_cast<uintptr_t>(pq.phys_addr);
    }

    // ---------- Typed reads / writes ----------

    template <typename T>
    [[nodiscard]] T ReadMem(uintptr_t addr) const
    {
        static_assert(std::is_trivially_copyable_v<T>, "ReadMem only supports trivially copyable types");
        T out{};

        if (!ReadRaw(addr, &out, sizeof(T)))
        {
            //std::cerr << "[DEBUG] Failed operation ReadRaw at address 0x" << std::hex << addr << std::dec << std::endl;
        }

        return out;
    }

    // Same as ReadMem but goes through the physical page-pin path.
    template <typename T>
    [[nodiscard]] T ReadMemPhysical(uintptr_t addr) const
    {
        static_assert(std::is_trivially_copyable_v<T>, "ReadMemPhysical only supports trivially copyable types");
        T out{};
        if (!ReadPhysical(addr, &out, sizeof(T)))
        {
            // ERR
        }
        return out;
    }

    template <typename T>
    [[nodiscard]] bool WriteMem(uintptr_t addr, const T& value) const
    {
        static_assert(std::is_trivially_copyable_v<T>, "WriteMem only supports trivially copyable types");
        return WriteRaw(addr, &value, sizeof(T));
    }

    // ---------- Array helpers ----------

    template <typename T>
    [[nodiscard]] std::vector<T> ReadArray(uintptr_t address, size_t count) const
    {
        static_assert(std::is_trivially_copyable_v<T>, "ReadArray only supports trivially copyable types");
        std::vector<T> result(count);
        if (count == 0 || address == 0)
            return result;

        if (!ReadRaw(address, result.data(), sizeof(T) * count))
            result.clear();

        return result;
    }

    template <typename T>
    bool ReadArraySafe(uintptr_t address, size_t count, std::vector<T>& out) const
    {
        static_assert(std::is_trivially_copyable_v<T>, "ReadArraySafe only supports trivially copyable types");
        if (count == 0 || address == 0)
            return false;

        out.resize(count);
        return ReadRaw(address, out.data(), sizeof(T) * count);
    }

    // ---------- String ----------

    [[nodiscard]] std::string ReadString(uintptr_t address, size_t maxLength = 256) const
    {
        if (address == 0)
            return {};

        std::vector<char> buffer(maxLength);
        if (!ReadRaw(address, buffer.data(), maxLength))
            return {};

        buffer[maxLength - 1] = '\0';
        return std::string(buffer.data());
    }

    // ---------- Module info ----------

    struct ModuleInfo
    {
        uintptr_t   base{};
        size_t      size{};
        std::string path;
    };

    class ModuleCache
    {
    public:
        [[nodiscard]] std::optional<ModuleInfo> Get(const std::string& name) const
        {
            std::scoped_lock lock(m_mutex);
            auto it = m_cache.find(name);
            if (it != m_cache.end())
                return it->second;
            return std::nullopt;
        }

        void Set(const std::string& name, const ModuleInfo& info)
        {
            std::scoped_lock lock(m_mutex);
            m_cache[name] = info;
        }

        void Clear()
        {
            std::scoped_lock lock(m_mutex);
            m_cache.clear();
        }

    private:
        std::unordered_map<std::string, ModuleInfo> m_cache;
        mutable std::mutex                          m_mutex;
    };

    static ModuleCache g_moduleCache;

    static std::optional<ModuleInfo> GetModuleInfoFromMaps(pid_t pid, const std::string& moduleName);

    static std::optional<ModuleInfo> GetModuleInfo(pid_t pid, const std::string& moduleName, bool useCache = true);

    [[nodiscard]] uintptr_t GetModuleBase(const std::string& moduleName, bool useCache = true) const
    {
        auto info = GetModuleInfo(m_pid, moduleName, useCache);
        return info.has_value() ? info->base : 0;
    }

    static size_t GetModuleSize(pid_t pid, const std::string& moduleName, bool useCache = true)
    {
        auto info = GetModuleInfo(pid, moduleName, useCache);
        return info.has_value() ? info->size : 0;
    }

    static void ClearCache()
    {
        g_moduleCache.Clear();
    }

    // ---------- Pattern scanning ----------

    [[nodiscard]] uintptr_t PatternScan(const std::string& moduleName,
                                        const std::vector<uint8_t>& pattern,
                                        const std::string& mask) const;

    // IDA-style: "48 8B ? ? ? ? 48 89"
    [[nodiscard]] uintptr_t PatternScan(const std::string& moduleName, const std::string& idaPattern) const;

    [[nodiscard]] std::vector<uintptr_t> PatternScanAll(const std::string& moduleName,
                                                        const std::vector<uint8_t>& pattern,
                                                        const std::string& mask) const;

    // ---------- Address resolution ----------

    [[nodiscard]] uintptr_t ResolveRelativeAddress(uintptr_t address, uint32_t offset, uint32_t instructionSize) const
    {
        if (!address)
            return 0;
        const auto relOffset = ReadMem<int32_t>(address + offset);
        return address + instructionSize + relOffset;
    }

    [[nodiscard]] uintptr_t GetAbsoluteAddress(uintptr_t instructionPtr, uint32_t offset = 1, uint32_t size = 5) const
    {
        return ResolveRelativeAddress(instructionPtr, offset, size);
    }

    // ---------- Bounds check ----------

    [[nodiscard]] bool IsAddressInModule(uintptr_t address, const std::string& moduleName) const
    {
        const auto info = GetModuleInfo(m_pid, moduleName);
        if (!info.has_value())
            return false;
        return address >= info->base && address < (info->base + info->size);
    }

    // ---------- Process discovery ----------

    static std::vector<pid_t> FindPidsByName(const std::string& name);

    // ---------- Source2 interface resolution ----------

    [[nodiscard]] uintptr_t GetSegmentFromPHT(uintptr_t base_address, uint32_t tag) const;
    [[nodiscard]] uintptr_t GetAddressFromDynamicSection(uintptr_t base_address, uint64_t tag) const;
    [[nodiscard]] uintptr_t GetModuleExport(uintptr_t base_address, std::string_view export_name) const;
    [[nodiscard]] std::optional<uintptr_t> GetInterfaceOffset(uintptr_t base_address,std::string_view interface_name) const;


    // ---------- Singleton ----------

    static CUtils& Get()
    {
        if (!s_instance)
            throw std::runtime_error("CUtils: not initialised — call CUtils::Init() first");
        return *s_instance;
    }

    // Call once at startup; subsequent calls are no-ops
    static CUtils& Init(int fd, pid_t pid)
    {
        if (!s_instance)
            s_instance = std::make_unique<CUtils>(fd, pid);
        return *s_instance;
    }

    static bool IsInitialised() { return s_instance != nullptr; }

    // Explicit teardown (optional — destructor handles fd close)
    static void Shutdown() { s_instance.reset(); }

private:
    static std::unique_ptr<CUtils> s_instance;
};

inline static CUtils& R() { return CUtils::Get(); }

inline static std::string remote_str(uintptr_t p_char, size_t max_len = 256)
{
    if (!p_char) return "<null>";
    return R().ReadString(p_char, max_len);
}

inline static bool is_valid_ptr(uintptr_t p)
{
    return p > 0x400000 && p != k_sentinel && (p & 0x3) == 0;
}

inline static bool is_valid_str_ptr(uintptr_t p)
{
    return p > 0x400000 && p != k_sentinel; // no alignment requirement for char*
}


inline static bool is_valid_struct_ptr(uintptr_t p)
{
    return p > 0x400000 && p != k_sentinel && (p & 0x3) == 0;
}

namespace Utils::Math
{
    struct Vector
    {
        float x,y,z;

        // Operator+ for vector addition
        Vector operator+(const Vector& other) const
        {
            return { x + other.x, y + other.y, z + other.z };
        }

        // Optional: operator+= for in-place addition
        Vector& operator+=(const Vector& other)
        {
            x += other.x;
            y += other.y;
            z += other.z;
            return *this;
        }
    };

    inline float v_matrix[4][4]{};
    inline uintptr_t vmatrix_addr = 0;

    // WorldToScreen returns std::optional<ImVec2>.
    // - std::nullopt if the point is behind the camera or off-screen
    inline std::optional<ImVec2> WorldToScreen(const Vector& world, float screen_width, float screen_height)
    {
        if (!R().ReadRaw(Utils::Math::vmatrix_addr , Utils::Math::v_matrix, sizeof(Utils::Math::v_matrix)))
            return std::nullopt;

        // Transform world coordinates into clip space
        float clip_x = v_matrix[0][0]*world.x + v_matrix[0][1]*world.y + v_matrix[0][2]*world.z + v_matrix[0][3];
        float clip_y = v_matrix[1][0]*world.x + v_matrix[1][1]*world.y + v_matrix[1][2]*world.z + v_matrix[1][3];
        float clip_w = v_matrix[3][0]*world.x + v_matrix[3][1]*world.y + v_matrix[3][2]*world.z + v_matrix[3][3];

        // Point is behind the camera
        if (clip_w < 0.001f)
            return std::nullopt;

        float inv_w = 1.0f / clip_w;

        // Normalize device coordinates
        float ndc_x = clip_x * inv_w;
        float ndc_y = clip_y * inv_w;

        // Convert to screen space
        float screen_x = (screen_width  / 2.0f) * (ndc_x + 1.0f);
        float screen_y = (screen_height / 2.0f) * (1.0f - ndc_y); // y inverted

        if (screen_x < 0.0f || screen_x > screen_width ||
            screen_y < 0.0f || screen_y > screen_height)
            return std::nullopt;

        return ImVec2(screen_x, screen_y);
    }
}

namespace Utils
{

    class mouse_injector
    {
    public:
        mouse_injector(int screen_width, int screen_height)
            : m_center_x(screen_width  / 2)
            , m_center_y(screen_height / 2)
        {
            m_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
            if (m_fd < 0)
                throw std::runtime_error("Failed to open /dev/uinput");
            setup_device(screen_width, screen_height);
        }

        ~mouse_injector()
        {
            ioctl(m_fd, UI_DEV_DESTROY);
            close(m_fd);
        }

        // ── Humanization state (NEW) ──────────────────────────────────────────────
        std::mt19937 m_rng       { std::random_device{}() };  // move inside the class
        float        m_f_vel_x   = 0.0f;   // inertia carry-over X
        float        m_f_vel_y   = 0.0f;   // inertia carry-over Y
        float        m_f_noise_x = 0.0f;   // low-freq wobble state X
        float        m_f_noise_y = 0.0f;   // low-freq wobble state Y
        int          m_i_skip_ctr = 0;     // dead-frame counter

        // FPS aimbot: game always locks cursor to center, so delta IS the target offset
        void aim_at(float target_x, float target_y, float smoothing = 1.0f)
        {
                  // ── RNG helpers ───────────────────────────────────────────────────────────
            auto f_gauss = [&](float stddev) -> float
            {
                std::normal_distribution<float> d(0.0f, stddev);
                return d(m_rng);
            };
            auto f_uniform = [&](float lo, float hi) -> float
            {
                std::uniform_real_distribution<float> d(lo, hi);
                return d(m_rng);
            };

            const float f_dx   = (target_x - m_center_x) * smoothing;
            const float f_dy   = (target_y - m_center_y) * smoothing;
            const float f_dist = std::hypot(f_dx, f_dy);

            // ── Factor 1: Dead frames (~5 % of ticks) ────────────────────────────────
            // Humans hesitate; a perfectly clocked input stream is a strong bot signal.
            if (m_i_skip_ctr > 0)
            {
                --m_i_skip_ctr;
                return;
            }
            if (f_uniform(0.0f, 1.0f) < 0.05f)
            {
                m_i_skip_ctr = static_cast<int>(f_uniform(1.0f, 4.0f));
                return;
            }

            // ── Factor 2: Inertia / momentum ─────────────────────────────────────────
            // Blends desired velocity into a running state — prevents instant reversal
            // and creates natural ease-in / ease-out around direction changes.
            constexpr float k_inertia = 0.62f;
            m_f_vel_x = m_f_vel_x * k_inertia + f_dx * (1.0f - k_inertia);
            m_f_vel_y = m_f_vel_y * k_inertia + f_dy * (1.0f - k_inertia);

            // ── Factor 3: Low-frequency path wobble (wrist / arm drift) ──────────────
            // Correlated noise that evolves smoothly across frames — not pure white
            // noise — so the path curves naturally rather than flickering.
            constexpr float k_wobble_decay  = 0.80f;
            constexpr float k_wobble_inject = 0.05f;
            m_f_noise_x = m_f_noise_x * k_wobble_decay + f_gauss(f_dist * k_wobble_inject);
            m_f_noise_y = m_f_noise_y * k_wobble_decay + f_gauss(f_dist * k_wobble_inject);

            float f_move_x = m_f_vel_x + m_f_noise_x;
            float f_move_y = m_f_vel_y + m_f_noise_y;

            // ── Factor 4: High-frequency micro-jitter (finger tremor) ────────────────
            // Small gaussian noise on every moving frame; suppressed near the target
            // so fine-aim doesn't vibrate excessively.
            if (f_dist > 2.0f)
            {
                f_move_x += f_gauss(0.18f);
                f_move_y += f_gauss(0.18f);
            }

            // ── Factor 5: Distance-adaptive speed variation ───────────────────────────
            // Humans flick quickly over large distances and crawl during fine aim.
            // A fixed per-pixel rate is trivially detectable.
            float f_speed_scale;
            if      (f_dist > 80.0f) f_speed_scale = f_uniform(1.06f, 1.20f); // flick burst
            else if (f_dist <  8.0f) f_speed_scale = f_uniform(0.76f, 0.94f); // fine-aim creep
            else                     f_speed_scale = f_uniform(0.96f, 1.04f); // mid-range jitter

            f_move_x *= f_speed_scale;
            f_move_y *= f_speed_scale;

            // ── Sub-pixel accumulation ────────────────────────────────────────────────
            m_accum_x += f_move_x;
            m_accum_y += f_move_y;

            const int ix = static_cast<int>(m_accum_x);
            const int iy = static_cast<int>(m_accum_y);

            if (ix == 0 && iy == 0)
                return;

            m_accum_x -= static_cast<float>(ix);
            m_accum_y -= static_cast<float>(iy);

            if (ix != 0) emit(EV_REL, REL_X, ix);
            if (iy != 0) emit(EV_REL, REL_Y, iy);
            emit(EV_SYN, SYN_REPORT, 0);
        }

        int center_x() const { return m_center_x; }
        int center_y() const { return m_center_y; }

    private:
        int   m_fd;
        int   m_center_x;
        int   m_center_y;
        float m_accum_x = 0.f;
        float m_accum_y = 0.f;

        void setup_device(int screen_width, int screen_height)
        {
            ioctl(m_fd, UI_SET_PROPBIT, INPUT_PROP_POINTER);

            ioctl(m_fd, UI_SET_EVBIT,  EV_REL);
            ioctl(m_fd, UI_SET_RELBIT, REL_X);
            ioctl(m_fd, UI_SET_RELBIT, REL_Y);

            ioctl(m_fd, UI_SET_EVBIT,  EV_ABS);
            ioctl(m_fd, UI_SET_ABSBIT, ABS_X);
            ioctl(m_fd, UI_SET_ABSBIT, ABS_Y);

            ioctl(m_fd, UI_SET_EVBIT, EV_SYN);

            ioctl(m_fd, UI_SET_EVBIT,  EV_KEY);
            ioctl(m_fd, UI_SET_KEYBIT, BTN_LEFT);
            ioctl(m_fd, UI_SET_KEYBIT, BTN_RIGHT);
            ioctl(m_fd, UI_SET_KEYBIT, BTN_MIDDLE);

            uinput_abs_setup abs_x{};
            abs_x.code               = ABS_X;
            abs_x.absinfo.maximum    = screen_width;
            abs_x.absinfo.resolution = 1;
            ioctl(m_fd, UI_ABS_SETUP, &abs_x);

            uinput_abs_setup abs_y{};
            abs_y.code               = ABS_Y;
            abs_y.absinfo.maximum    = screen_height;
            abs_y.absinfo.resolution = 1;
            ioctl(m_fd, UI_ABS_SETUP, &abs_y);

            uinput_setup usetup{};
            usetup.id.bustype = BUS_USB;
            usetup.id.vendor  = 0x1234;
            usetup.id.product = 0x5678;
            std::strncpy(usetup.name, "virtual_mouse", UINPUT_MAX_NAME_SIZE);

            ioctl(m_fd, UI_DEV_SETUP,  &usetup);
            ioctl(m_fd, UI_DEV_CREATE);
            usleep(500'000);
        }

        void emit(int type, int code, int value)
        {
            input_event ev{};
            ev.type  = type;
            ev.code  = code;
            ev.value = value;
            write(m_fd, &ev, sizeof(ev));
        }
    };
    inline mouse_injector mouse{2560, 1440};
}

extern uintptr_t g_global_vars;

#endif // CS2_LINUXEXTERNAL_UTILS_H