#pragma once
// Common.h — shared types, constants, and includes for the whole project.
// If something is used in more than two files, it probably lives here.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

// Vulkan + GLM
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE  // Vulkan depth range is [0,1], not [-1,1]
// GLM_FORCE_DEFAULT_ALIGNED_GENTYPES deliberately NOT set here.
// It forces SIMD-aligned vec/mat types (glm::aligned_highp) which break
// operator* for mixed operand types — the infamous compute_vec_mul error.
// Our UBO struct uses alignas(16) explicitly, so we don't need GLM to force it.
#define GLM_ENABLE_EXPERIMENTAL      // required for gtx/* extensions
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>   // glm::angleAxis, glm::identity, etc.
#include <glm/gtc/type_ptr.hpp>

// STL
#include <array>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <functional>
#include <algorithm>
#include <memory>
#include <optional>
#include <cstdint>
#include <cmath>
#include <cassert>
#include <stdexcept>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>

// ── Logging ──────────────────────────────────────────────────────────────
enum class LogLevel { Info, Warning, Error };

inline void Log(LogLevel level, const std::string& msg) {
    const char* prefix = (level == LogLevel::Info)    ? "[INFO]"
                       : (level == LogLevel::Warning) ? "[WARN]"
                                                      : "[ERR ]";
    std::string full = std::string(prefix) + " " + msg + "\n";
    OutputDebugStringA(full.c_str());
    // Also print to stderr for visibility in the VS output window
    fputs(full.c_str(), stderr);
}

#define LOG_INFO(msg)  Log(LogLevel::Info,    msg)
#define LOG_WARN(msg)  Log(LogLevel::Warning, msg)
#define LOG_ERR(msg)   Log(LogLevel::Error,   msg)

// Vulkan error check — throws on failure with the call site and error code
#define VK_CHECK(call)                                                  \
    do {                                                                \
        VkResult _vkr = (call);                                         \
        if (_vkr != VK_SUCCESS) {                                       \
            char _buf[256];                                             \
            snprintf(_buf, sizeof(_buf),                                \
                "Vulkan error %d at %s:%d", (int)_vkr,                 \
                __FILE__, __LINE__);                                    \
            throw std::runtime_error(_buf);                             \
        }                                                               \
    } while(0)

// ── Physics / game constants ──────────────────────────────────────────────
constexpr float NEAR_Z         = 0.1f;
constexpr float CAP_RADIUS     = 0.35f;
constexpr float CAP_CYL_HALF   = 0.55f;
constexpr float CAP_HALF_H     = CAP_CYL_HALF + CAP_RADIUS; // 0.9
constexpr float EYE_OFFSET     = 0.74f;
constexpr float GROUND_Y       = 0.0f;
constexpr float GROUND_HALF    = 375.0f;  // 750x750m — big enough to get genuinely lost
constexpr float GRAVITY_A      = 9.8f;
constexpr float TERMINAL_V     = 28.0f;
constexpr float JUMP_VEL       = 6.4f;
constexpr float MOVE_SPEED     = 5.0f;
constexpr float SPRINT_MULT    = 1.85f;
constexpr float FCAM_SPEED     = 7.0f;
constexpr float PC_HALF        = 0.5f;
constexpr float PUSH_IMPULSE   = 7.5f;
constexpr float GRAB_DIST      = 2.4f;
constexpr float INTERACT_DIST  = 3.5f;
constexpr float PUSH_CD_TIME   = 0.35f;
constexpr float ENERGY_MAX     = 100.0f;
constexpr float ENERGY_DRAIN   = 22.0f;
constexpr float ENERGY_REGEN   = 11.0f;
constexpr float ENERGY_MIN_SPRINT = 5.0f;
constexpr float VOID_Y         = -85.0f;   // 85m below ground — spec says ~85m
constexpr int   MAX_FRAMES_IN_FLIGHT = 2;

// Spawn pos
inline const glm::vec3 SPAWN_POS = { 0.0f, CAP_HALF_H, -3.8f };
inline const glm::vec3 SUN_POS   = { 30.0f, 55.0f, 20.0f };

// ── App state ─────────────────────────────────────────────────────────────
enum class AppState { Loading, Title, Playing };

// ── Player movement state ─────────────────────────────────────────────────
enum class PMoveState { Idle, Walking, Sprinting };

// ── Window mode ───────────────────────────────────────────────────────────
enum class WinMode { Windowed, Borderless, Fullscreen };

// ── Vertex types ─────────────────────────────────────────────────────────
struct Vertex3D {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 uv;    // texture coordinates — {0,0} for untextured meshes
};

struct VertexShadow {
    glm::vec3 pos;
};

struct VertexUI {
    glm::vec2 pos;
    glm::vec2 uv;
    glm::vec4 color;
};

// ── UBO layouts (std140 aligned) ─────────────────────────────────────────
struct UBOFrame {
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
    alignas(16) glm::mat4 lightVP;
    alignas(16) glm::vec4 lightDir;
    alignas(16) glm::vec4 camPos;
    alignas(16) glm::vec4 skyCol;
    alignas(16) glm::vec4 gndCol;
    alignas(16) glm::vec4 fogParams;  // x=start y=end z=density w=unused
    alignas(16) glm::vec4 fogColor;
};

// Push constant for per-object data (128 bytes max — most GPUs support this)
struct PushConst {
    glm::mat4 model;
    glm::mat4 normalMat; // transpose(inverse(model))
};

// ── Frustum ───────────────────────────────────────────────────────────────
struct Frustum {
    glm::vec4 planes[6]; // left right bottom top near far
};

// ── Helper: formatted string ──────────────────────────────────────────────
inline std::string Fmt(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return std::string(buf);
}

// ── Helper: exe directory ─────────────────────────────────────────────────
inline std::string ExeDir() {
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string p(buf);
    auto s = p.find_last_of("\\/");
    return s == std::string::npos ? "" : p.substr(0, s + 1);
}
