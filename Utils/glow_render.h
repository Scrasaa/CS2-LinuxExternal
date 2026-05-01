#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  GlowRenderer — OpenGL 3.x port of the D3D11 VS_Glow / PS_Glow pipeline.
//
//  Drop-in for your X11/GLX overlay.  Call once:
//      GlowRenderer::init();
//  Each frame before ImGui::Render():
//      GlowRenderer::begin_frame(screen_w, screen_h);
//      GlowRenderer::submit(cmd);  // one per player
//      GlowRenderer::flush();
//      GlowRenderer::end_frame();  // restores GL state for ImGui
// ─────────────────────────────────────────────────────────────────────────────

#include <GL/gl.h>
#include <GL/glext.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>
#include <array>

// ── GLSL sources ─────────────────────────────────────────────────────────────

// Vertex layout (interleaved, tightly packed):
//   location 0 — vec3  position        (model-space)
//   location 1 — vec3  normal          (model-space)
//   location 2 — vec2  uv
//   location 3 — vec4  bone_weights
//   location 4 — uvec4 bone_indices    (uint)

static constexpr const char* k_vs_glow_src = R"GLSL(
#version 330 core

layout(location = 0) in vec3  a_Position;
layout(location = 1) in vec3  a_Normal;
layout(location = 2) in vec2  a_UV;
layout(location = 3) in vec4  a_BoneWeights;
layout(location = 4) in uvec4 a_BoneIndices;

// Bone palette — matches D3D11 g_BoneMatrices[]
// Max 256 bones; upload only as many as the mesh uses.
layout(std140) uniform UBO_Bones
{
    mat4 u_BoneMatrices[256];
};

// Per-draw material — matches cb_material_t
layout(std140) uniform UBO_Material
{
    vec4  u_RimColor;       // rgb + alpha
    float u_RimPower;       // glow thickness in pixels
    float u_Alpha;
    vec3  u_PlayerOrigin;   // world-space
    float u_ScreenW;
    float u_ScreenH;
    float u_BBoxMinY;       // NDC Y of player bbox top/bottom
    float u_BBoxMaxY;
    float _pad0;            // keep 16-byte stride
};

out vec3  v_WorldPos;
out vec3  v_WorldNormal;
out float v_EdgeT;          // 0 = inner (base mesh), 1 = outer tip
out vec2  v_UV;

void main()
{
    // ── Skinning ─────────────────────────────────────────────────────────────
    vec4 skinned_pos    = vec4(0.0);
    vec3 skinned_normal = vec3(0.0);

    for (int i = 0; i < 4; ++i)
    {
        float w = a_BoneWeights[i];
        if (w > 0.0001)
        {
            uint  bi  = a_BoneIndices[i];
            mat4  bm  = u_BoneMatrices[bi];
            skinned_pos    += bm * vec4(a_Position, 1.0) * w;
            skinned_normal += mat3(bm) * a_Normal          * w;
        }
    }

    // ── Distance guard — discard degenerate stretched bones ──────────────────
    vec3 d = skinned_pos.xyz - u_PlayerOrigin;
    if (dot(d, d) > 250.0 * 250.0)
    {
        gl_Position = vec4(0.0);
        v_EdgeT     = 0.0;
        return;
    }

    // ── Project to clip space ─────────────────────────────────────────────────
    // gl_Position is set by the MVP supplied as a plain uniform so the glow
    // VS can share the same bone UBO with the stencil pass.
    // We compute clip coords manually to replicate the exact NDC expansion.
    // Upload gl_ModelViewProjectionMatrix or pass u_VP separately — see below.

    // NOTE: upload the combined ViewProjection as u_VP (mat4 uniform).
    // We keep it outside the UBO so the bone UBO binding stays stable.
    // (declared below as a plain uniform)

    // ── Screen-space normal expansion (identical logic to HLSL VS_Glow) ──────
    vec3  N          = normalize(skinned_normal);
    vec4  clip       = gl_Position; // set after MVP multiply below — see main2

    v_WorldPos    = skinned_pos.xyz;
    v_WorldNormal = N;
    v_UV          = a_UV;
    v_EdgeT       = 0.0; // overridden per pass
}
)GLSL";

// The real vertex shader — VS_Glow properly, with u_VP and EdgeT.
// (Replacing the stub above with the full version used at runtime.)
static constexpr const char* k_vs_glow_full = R"GLSL(
#version 330 core

layout(location = 0) in vec3  a_Position;
layout(location = 1) in vec3  a_Normal;
layout(location = 2) in vec2  a_UV;
layout(location = 3) in vec4  a_BoneWeights;
layout(location = 4) in uvec4 a_BoneIndices;

layout(std140) uniform UBO_Bones
{
    mat4 u_BoneMatrices[256];
};

layout(std140) uniform UBO_Material
{
    vec4  u_RimColor;
    float u_RimPower;
    float u_Alpha;
    vec3  u_PlayerOrigin;
    float u_ScreenW;
    float u_ScreenH;
    float u_BBoxMinY;
    float u_BBoxMaxY;
    float _pad0;
};

uniform mat4  u_VP;         // ViewProjection — set once per frame
uniform float u_EdgeT_val;  // 0.0 (stencil/fill pass) or 1.0 (glow pass)

out vec3  v_WorldPos;
out vec3  v_WorldNormal;
out float v_EdgeT;
out vec2  v_UV;

void main()
{
    // ── Skinning ──────────────────────────────────────────────────────────────
    vec4 skinned_pos    = vec4(0.0);
    vec3 skinned_normal = vec3(0.0);

    for (int i = 0; i < 4; ++i)
    {
        float w  = a_BoneWeights[i];
        if (w > 0.0001)
        {
            uint  bi = a_BoneIndices[i];
            mat4  bm = u_BoneMatrices[bi];
            skinned_pos    += bm * vec4(a_Position, 1.0) * w;
            skinned_normal += mat3(bm) * a_Normal * w;
        }
    }

    // ── Distance guard ────────────────────────────────────────────────────────
    vec3 delta = skinned_pos.xyz - u_PlayerOrigin;
    if (dot(delta, delta) > 62500.0) // 250^2
    {
        gl_Position = vec4(0.0);
        v_EdgeT     = 0.0;
        return;
    }

    // ── Clip space ────────────────────────────────────────────────────────────
    vec4 clip = u_VP * skinned_pos;
    if (clip.w < 0.1) clip.w = 0.1;

    // ── Screen-space normal → NDC expansion ──────────────────────────────────
    vec3  N = normalize(skinned_normal);

    // Project normal direction into clip space (w=0 strips translation)
    vec4 clip_n;
    clip_n.x = dot(u_VP[0].xyz, N);
    clip_n.y = dot(u_VP[1].xyz, N);
    clip_n.z = 0.0;
    clip_n.w = 0.0;

    // NDC normal — perspective-correct expansion
    float inv_w    = 1.0 / clip.w;
    vec2  ndc_norm = vec2(clip_n.x, clip_n.y) * inv_w;

    float ndc_len = length(ndc_norm);

    // Pixel → NDC: base position
    float sx = u_ScreenW * 0.5 + (clip.x * inv_w) * u_ScreenW * 0.5 + 0.5;
    float sy = u_ScreenH * 0.5 - (clip.y * inv_w) * u_ScreenH * 0.5 + 0.5;
    vec2  base_ndc;
    base_ndc.x =  (sx / u_ScreenW) * 2.0 - 1.0;
    base_ndc.y = -((sy / u_ScreenH) * 2.0 - 1.0);

    if (ndc_len < 0.0001 || u_EdgeT_val < 0.5)
    {
        // Stencil / fill pass — emit at base position, no expansion
        gl_Position = vec4(base_ndc, clamp(clip.w * 0.0001, 0.0, 1.0), 1.0);
        v_EdgeT     = 0.0;
        v_WorldPos    = skinned_pos.xyz;
        v_WorldNormal = N;
        v_UV          = a_UV;
        return;
    }

    ndc_norm /= ndc_len;

    // Clamp expansion to 8% of player screen height (4% per side)
    float bbox_h_ndc     = abs(u_BBoxMaxY - u_BBoxMinY);
    float max_expand_y   = max(bbox_h_ndc * 0.04, 2.0 / u_ScreenH);

    vec2  expand_raw = ndc_norm * u_RimPower * vec2(2.0 / u_ScreenW, 2.0 / u_ScreenH);
    float expand_len = length(expand_raw);
    vec2  expand     = (expand_len > max_expand_y)
                       ? (expand_raw / expand_len) * max_expand_y
                       : expand_raw;

    gl_Position   = vec4(base_ndc + expand, clamp(clip.w * 0.0001, 0.0, 1.0), 1.0);
    v_EdgeT       = 1.0;
    v_WorldPos    = skinned_pos.xyz;
    v_WorldNormal = N;
    v_UV          = a_UV;
}
)GLSL";

// ── Fragment shaders ──────────────────────────────────────────────────────────

static constexpr const char* k_fs_fill_src = R"GLSL(
#version 330 core
out vec4 frag_color;
void main()
{
    // Stencil-only pass — writes nothing to color
    frag_color = vec4(0.0);
}
)GLSL";

static constexpr const char* k_fs_glow_src = R"GLSL(
#version 330 core

layout(std140) uniform UBO_Material
{
    vec4  u_RimColor;
    float u_RimPower;
    float u_Alpha;
    vec3  u_PlayerOrigin;
    float u_ScreenW;
    float u_ScreenH;
    float u_BBoxMinY;
    float u_BBoxMaxY;
    float _pad0;
};

in  float v_EdgeT;
out vec4  frag_color;

void main()
{
    // AA over inner edge — mirrors PS_Glow smoothstep
    float aa_width = clamp(1.5 / max(u_RimPower, 1.0), 0.0, 0.5);
    float aa       = smoothstep(0.0, aa_width, v_EdgeT);

    frag_color = vec4(clamp(u_RimColor.rgb, 0.0, 1.0),
                      u_RimColor.a * u_Alpha * aa);
}
)GLSL";

// ─────────────────────────────────────────────────────────────────────────────
//  GlowRenderer
// ─────────────────────────────────────────────────────────────────────────────

namespace GlowRenderer
{

// ── Vertex layout ─────────────────────────────────────────────────────────────

struct Vertex
{
    float    position[3];
    float    normal[3];
    float    uv[2];
    float    bone_weights[4];
    uint32_t bone_indices[4]; // packed as uint
};

// ── Material constant buffer (mirrors cb_material_t, std140) ─────────────────
// std140 padding: each member starts at its natural alignment, vec3 = 16 bytes.

struct alignas(16) Material_UBO
{
    float rim_color[4];       // vec4
    float rim_power;          // float
    float alpha;              // float
    float _pad1[2];           // pad to 16
    float player_origin[4];   // vec3 → padded to vec4
    float screen_w;
    float screen_h;
    float bbox_min_y;
    float bbox_max_y;
    float _pad2[4];           // 16-byte close
};

// ── Draw command (one per player) ─────────────────────────────────────────────

struct GlowCmd
{
    // Geometry
    const Vertex*   vertices      = nullptr;
    uint32_t        vertex_count  = 0;
    const uint32_t* indices       = nullptr;
    uint32_t        index_count   = 0;

    // Bone palette (CPU-side, row-major float[4][4])
    const float (*bone_matrices)[4][4] = nullptr;
    uint32_t     bone_count            = 0;

    // ViewProjection (column-major, as glm / DirectXMath output)
    float vp[16];

    // Glow appearance
    float glow_color[4];   // r,g,b,a  0-1
    float glow_thickness;  // pixels
    float alpha;

    // World
    float player_origin[3];
    float screen_w;
    float screen_h;
    float bbox_min_y;
    float bbox_max_y;
};

// ── Internal state ────────────────────────────────────────────────────────────

namespace detail
{
    // OpenGL function pointers (loaded once from the driver via glXGetProcAddress)
    // We only use what we need — avoids pulling in GLEW/GLAD.

    #define GLOW_GL_FUNCS \
        X(PFNGLGENBUFFERSPROC,              glGenBuffers) \
        X(PFNGLDELETEBUFFERSPROC,           glDeleteBuffers) \
        X(PFNGLBINDBUFFERPROC,              glBindBuffer) \
        X(PFNGLBUFFERDATAPROC,              glBufferData) \
        X(PFNGLBUFFERSUBDATAPROC,           glBufferSubData) \
        X(PFNGLGENVERTEXARRAYSPROC,         glGenVertexArrays) \
        X(PFNGLDELETEVERTEXARRAYSPROC,      glDeleteVertexArrays) \
        X(PFNGLBINDVERTEXARRAYPROC,         glBindVertexArray) \
        X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) \
        X(PFNGLVERTEXATTRIBPOINTERPROC,     glVertexAttribPointer) \
        X(PFNGLVERTEXATTRIBIPOINTERPROC,    glVertexAttribIPointer) \
        X(PFNGLCREATESHADERPROC,            glCreateShader) \
        X(PFNGLDELETESHADERPROC,            glDeleteShader) \
        X(PFNGLSHADERSOURCEPROC,            glShaderSource) \
        X(PFNGLCOMPILESHADERPROC,           glCompileShader) \
        X(PFNGLGETSHADERIVPROC,             glGetShaderiv) \
        X(PFNGLGETSHADERINFOLOGPROC,        glGetShaderInfoLog) \
        X(PFNGLCREATEPROGRAMPROC,           glCreateProgram) \
        X(PFNGLDELETEPROGRAMPROC,           glDeleteProgram) \
        X(PFNGLATTACHSHADERPROC,            glAttachShader) \
        X(PFNGLLINKPROGRAMPROC,             glLinkProgram) \
        X(PFNGLGETPROGRAMIVPROC,            glGetProgramiv) \
        X(PFNGLGETPROGRAMINFOLOGPROC,       glGetProgramInfoLog) \
        X(PFNGLUSEPROGRAMPROC,              glUseProgram) \
        X(PFNGLGETUNIFORMLOCATIONPROC,      glGetUniformLocation) \
        X(PFNGLUNIFORM1FPROC,               glUniform1f) \
        X(PFNGLUNIFORMMATRIX4FVPROC,        glUniformMatrix4fv) \
        X(PFNGLGETUNIFORMBLOCKINDEXPROC,    glGetUniformBlockIndex) \
        X(PFNGLUNIFORMBLOCKBINDINGPROC,     glUniformBlockBinding) \
        X(PFNGLBINDBUFFERBASEPROC,          glBindBufferBase) \
        X(PFNGLGENRENDERBUFFERSPROC,        glGenRenderbuffers) \
        X(PFNGLDELETERENDERBUFFERSPROC,     glDeleteRenderbuffers) \
        X(PFNGLBINDRENDERBUFFERPROC,        glBindRenderbuffer) \
        X(PFNGLRENDERBUFFERSTORAGEPROC,     glRenderbufferStorage) \
        X(PFNGLGENFRAMEBUFFERSPROC,         glGenFramebuffers) \
        X(PFNGLDELETEFRAMEBUFFERSPROC,      glDeleteFramebuffers) \
        X(PFNGLBINDFRAMEBUFFERPROC,         glBindFramebuffer) \
        X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer) \
        X(PFNGLCHECKFRAMEBUFFERSTATUSPROC,  glCheckFramebufferStatus)

    #define X(type, name) static type name = nullptr;
    GLOW_GL_FUNCS
    #undef X

    static bool         s_init       = false;
    static GLuint       s_prog_fill  = 0; // stencil write (color masked)
    static GLuint       s_prog_glow  = 0; // glow layers
    static GLuint       s_vao        = 0;
    static GLuint       s_vbo        = 0; // dynamic, re-uploaded per cmd
    static GLuint       s_ebo        = 0;
    static GLuint       s_ubo_bones  = 0; // binding 0
    static GLuint       s_ubo_mat    = 0; // binding 1
    static GLuint       s_fbo        = 0; // owns depth+stencil attachment
    static GLuint       s_rbo_ds     = 0; // depth24_stencil8 renderbuffer
    static int          s_fb_w       = 0;
    static int          s_fb_h       = 0;

    // Uniform locations in s_prog_glow
    static GLint        s_loc_vp_fill = -1;
    static GLint        s_loc_edge_fill = -1;
    static GLint        s_loc_vp_glow = -1;
    static GLint        s_loc_edge_glow = -1;

    // ── GL proc loader ────────────────────────────────────────────────────────

    static bool load_procs()
    {
        bool ok = true;
        #define X(type, name) \
            name = reinterpret_cast<type>(glXGetProcAddressARB( \
                reinterpret_cast<const GLubyte*>(#name))); \
            if (!name) { fprintf(stderr, "[glow] missing " #name "\n"); ok = false; }
        GLOW_GL_FUNCS
        #undef X
        return ok;
    }

    // ── Shader compiler ───────────────────────────────────────────────────────

    static GLuint compile_shader(GLenum type, const char* src)
    {
        GLuint sh = glCreateShader(type);
        glShaderSource(sh, 1, &src, nullptr);
        glCompileShader(sh);

        GLint ok = 0;
        glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            char buf[1024];
            glGetShaderInfoLog(sh, sizeof(buf), nullptr, buf);
            fprintf(stderr, "[glow] shader compile error:\n%s\n", buf);
            glDeleteShader(sh);
            return 0;
        }
        return sh;
    }

    static GLuint link_program(const char* vs_src, const char* fs_src)
    {
        GLuint vs = compile_shader(GL_VERTEX_SHADER,   vs_src);
        GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
        if (!vs || !fs) return 0;

        GLuint prog = glCreateProgram();
        glAttachShader(prog, vs);
        glAttachShader(prog, fs);
        glLinkProgram(prog);

        GLint ok = 0;
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            char buf[1024];
            glGetProgramInfoLog(prog, sizeof(buf), nullptr, buf);
            fprintf(stderr, "[glow] link error:\n%s\n", buf);
            glDeleteProgram(prog);
            prog = 0;
        }

        glDeleteShader(vs);
        glDeleteShader(fs);
        return prog;
    }

    // ── UBO block binding helper ──────────────────────────────────────────────

    static void bind_ubo_block(GLuint prog, const char* block_name, GLuint binding)
    {
        GLuint idx = glGetUniformBlockIndex(prog, block_name);
        if (idx != GL_INVALID_INDEX)
            glUniformBlockBinding(prog, idx, binding);
    }

    // ── Framebuffer with depth+stencil ────────────────────────────────────────

    static bool create_fbo(int w, int h)
    {
        glGenFramebuffers(1, &s_fbo);
        glGenRenderbuffers(1, &s_rbo_ds);

        glBindRenderbuffer(GL_RENDERBUFFER, s_rbo_ds);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);

        glBindFramebuffer(GL_FRAMEBUFFER, s_fbo);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, s_rbo_ds);

        // No color attachment — glow uses the default (window) color buffer,
        // we borrow only the depth+stencil from this RBO by blitting.
        // Simpler: just keep FBO unbound and use glEnable(GL_STENCIL_TEST)
        // against the default framebuffer's stencil.
        // Most GLX ARGB visuals include a stencil buffer — check at init.
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        s_fb_w = w;
        s_fb_h = h;
        return true;
    }

} // namespace detail

// ─────────────────────────────────────────────────────────────────────────────
//  Public API
// ─────────────────────────────────────────────────────────────────────────────

// Call once after glXMakeCurrent.
// screen_w / screen_h — initial overlay dimensions.
inline bool init(int screen_w, int screen_h)
{
    using namespace detail;

    if (s_init) return true;

    if (!load_procs())
        return false;

    // ── Compile programs ──────────────────────────────────────────────────────
    s_prog_fill = link_program(k_vs_glow_full, k_fs_fill_src);
    s_prog_glow = link_program(k_vs_glow_full, k_fs_glow_src);
    if (!s_prog_fill || !s_prog_glow)
        return false;

    // Bind UBO slots for both programs
    for (GLuint prog : { s_prog_fill, s_prog_glow })
    {
        bind_ubo_block(prog, "UBO_Bones",    0);
        bind_ubo_block(prog, "UBO_Material", 1);
    }

    // Cache uniform locations
    s_loc_vp_fill   = glGetUniformLocation(s_prog_fill, "u_VP");
    s_loc_edge_fill = glGetUniformLocation(s_prog_fill, "u_EdgeT_val");
    s_loc_vp_glow   = glGetUniformLocation(s_prog_glow, "u_VP");
    s_loc_edge_glow = glGetUniformLocation(s_prog_glow, "u_EdgeT_val");

    // ── VAO / VBO / EBO ───────────────────────────────────────────────────────
    glGenVertexArrays(1, &s_vao);
    glBindVertexArray(s_vao);

    glGenBuffers(1, &s_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);

    glGenBuffers(1, &s_ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_ebo);

    // position — float3
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, position)));
    // normal — float3
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, normal)));
    // uv — float2
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, uv)));
    // bone_weights — float4
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, bone_weights)));
    // bone_indices — uint4 (IPointer — no float conversion)
    glEnableVertexAttribArray(4);
    glVertexAttribIPointer(4, 4, GL_UNSIGNED_INT, sizeof(Vertex),
                           reinterpret_cast<void*>(offsetof(Vertex, bone_indices)));

    glBindVertexArray(0);

    // ── UBOs ─────────────────────────────────────────────────────────────────
    glGenBuffers(1, &s_ubo_bones);
    glBindBuffer(GL_UNIFORM_BUFFER, s_ubo_bones);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(float) * 16 * 256, nullptr, GL_DYNAMIC_DRAW);

    glGenBuffers(1, &s_ubo_mat);
    glBindBuffer(GL_UNIFORM_BUFFER, s_ubo_mat);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(Material_UBO), nullptr, GL_DYNAMIC_DRAW);

    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    create_fbo(screen_w, screen_h);

    s_init = true;
    return true;
}

// Call at start of each frame (before ImGui::NewFrame).
inline void begin_frame(int /*screen_w*/, int /*screen_h*/)
{
    // Clear stencil so previous frame's masks don't bleed through
    glClear(GL_STENCIL_BUFFER_BIT);
}

// Submit one player's glow command.
// All GL state is self-contained — restored to ImGui-compatible defaults after flush().
inline void submit(const GlowCmd& cmd)
{
    using namespace detail;
    if (!s_init) return;

    // ── Upload geometry ───────────────────────────────────────────────────────
    glBindVertexArray(s_vao);

    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(cmd.vertex_count * sizeof(Vertex)),
                 cmd.vertices, GL_STREAM_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(cmd.index_count * sizeof(uint32_t)),
                 cmd.indices, GL_STREAM_DRAW);

    // ── Upload bone matrices ──────────────────────────────────────────────────
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, s_ubo_bones);
    glBindBuffer(GL_UNIFORM_BUFFER, s_ubo_bones);
    glBufferSubData(GL_UNIFORM_BUFFER, 0,
                    static_cast<GLsizeiptr>(cmd.bone_count * sizeof(float) * 16),
                    cmd.bone_matrices);

    // ─────────────────────────────────────────────────────────────────────────
    //  Pass 1 — Stencil write (mirrors D3D11 m_dss_stencil_write)
    //  Writes stencil=1 wherever the mesh covers; no color output.
    // ─────────────────────────────────────────────────────────────────────────
    {
        // Material UBO (only player_origin and screen dims needed here)
        Material_UBO mat{};
        mat.rim_color[0]   = 0.f;
        mat.rim_color[1]   = 0.f;
        mat.rim_color[2]   = 0.f;
        mat.rim_color[3]   = 0.f;
        mat.rim_power      = 0.f;
        mat.alpha          = 0.f;
        mat.player_origin[0] = cmd.player_origin[0];
        mat.player_origin[1] = cmd.player_origin[1];
        mat.player_origin[2] = cmd.player_origin[2];
        mat.screen_w       = cmd.screen_w;
        mat.screen_h       = cmd.screen_h;
        mat.bbox_min_y     = cmd.bbox_min_y;
        mat.bbox_max_y     = cmd.bbox_max_y;

        glBindBufferBase(GL_UNIFORM_BUFFER, 1, s_ubo_mat);
        glBindBuffer(GL_UNIFORM_BUFFER, s_ubo_mat);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(mat), &mat);

        glUseProgram(s_prog_fill);
        glUniformMatrix4fv(s_loc_vp_fill, 1, GL_FALSE, cmd.vp);
        glUniform1f(s_loc_edge_fill, 0.0f); // inner/base — no expansion

        // Depth: always pass, no depth write
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_ALWAYS);
        glDepthMask(GL_FALSE);

        // Stencil: always pass, replace with ref=1
        glEnable(GL_STENCIL_TEST);
        glStencilMask(0xFF);
        glStencilFunc(GL_ALWAYS, 1, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

        // Colour mask off — stencil only
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

        // No face culling (match D3D11 CULL_NONE)
        glDisable(GL_CULL_FACE);

        glDrawElements(GL_TRIANGLES,
                       static_cast<GLsizei>(cmd.index_count),
                       GL_UNSIGNED_INT, nullptr);
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Pass 2 — Glow layers (mirrors D3D11 m_dss_stencil_outline + additive)
    //  Draws only where stencil != 1 (outside the mesh silhouette).
    //  Additive blend with expanded vertices → soft glow rim.
    // ─────────────────────────────────────────────────────────────────────────
    {
        // Restore colour writes
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        // Stencil: pass only where stencil != 1 (the outline region)
        glStencilMask(0x00); // no stencil writes in this pass
        glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

        // Depth off — glow renders regardless of scene depth
        glDisable(GL_DEPTH_TEST);

        // Additive blend: src_alpha * src + 1 * dst
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glBlendEquation(GL_FUNC_ADD);

        glUseProgram(s_prog_glow);
        glUniformMatrix4fv(s_loc_vp_glow, 1, GL_FALSE, cmd.vp);
        glUniform1f(s_loc_edge_glow, 1.0f); // outer tip

        // Four layers — matches k_layers[] in the D3D11 flush()
        struct Layer { float thickness_scale; float alpha_scale; };
        static constexpr Layer k_layers[] =
        {
            { 1.0f, 0.90f },
            { 1.8f, 0.55f },
            { 3.0f, 0.30f },
            { 5.0f, 0.12f },
        };
        static constexpr float k_alpha_sum  = 0.90f + 0.55f + 0.30f + 0.12f;
        static constexpr float k_alpha_norm = 1.0f / k_alpha_sum;

        for (const auto& layer : k_layers)
        {
            Material_UBO mat{};
            mat.rim_color[0]   = cmd.glow_color[0];
            mat.rim_color[1]   = cmd.glow_color[1];
            mat.rim_color[2]   = cmd.glow_color[2];
            mat.rim_color[3]   = cmd.glow_color[3] * layer.alpha_scale * k_alpha_norm;
            mat.rim_power      = cmd.glow_thickness * layer.thickness_scale;
            mat.alpha          = cmd.alpha * layer.alpha_scale * k_alpha_norm;
            mat.player_origin[0] = cmd.player_origin[0];
            mat.player_origin[1] = cmd.player_origin[1];
            mat.player_origin[2] = cmd.player_origin[2];
            mat.screen_w       = cmd.screen_w;
            mat.screen_h       = cmd.screen_h;
            mat.bbox_min_y     = cmd.bbox_min_y;
            mat.bbox_max_y     = cmd.bbox_max_y;

            glBindBuffer(GL_UNIFORM_BUFFER, s_ubo_mat);
            glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(mat), &mat);

            glDrawElements(GL_TRIANGLES,
                           static_cast<GLsizei>(cmd.index_count),
                           GL_UNSIGNED_INT, nullptr);
        }
    }

    glBindVertexArray(0);
}

// Call after all submit() calls, before ImGui::Render().
// Restores GL state to what ImGui_ImplOpenGL3 expects.
inline void end_frame()
{
    using namespace detail;

    // Restore state ImGui expects
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilMask(0xFF);
    glUseProgram(0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

// Call on overlay shutdown.
inline void shutdown()
{
    using namespace detail;
    if (!s_init) return;

    glDeleteProgram(s_prog_fill);
    glDeleteProgram(s_prog_glow);
    glDeleteVertexArrays(1, &s_vao);
    glDeleteBuffers(1, &s_vbo);
    glDeleteBuffers(1, &s_ebo);
    glDeleteBuffers(1, &s_ubo_bones);
    glDeleteBuffers(1, &s_ubo_mat);
    glDeleteFramebuffers(1, &s_fbo);
    glDeleteRenderbuffers(1, &s_rbo_ds);

    s_prog_fill = s_prog_glow = 0;
    s_vao = s_vbo = s_ebo = 0;
    s_ubo_bones = s_ubo_mat = 0;
    s_fbo = s_rbo_ds = 0;
    s_init = false;
}

} // namespace GlowRenderer

// ─────────────────────────────────────────────────────────────────────────────
//  Integration — paste into your Overlay.cpp run() like this:
//
//  In init(), after glXMakeCurrent:
//      GlowRenderer::init(g_ow.width, g_ow.height);
//
//  In run(), each frame, before ImGui::NewFrame():
//      GlowRenderer::begin_frame(g_ow.width, g_ow.height);
//
//  After entity cache refresh, per player:
//      GlowRenderer::GlowCmd cmd{};
//      cmd.vertices       = player_mesh.verts.data();
//      cmd.vertex_count   = player_mesh.verts.size();
//      cmd.indices        = player_mesh.indices.data();
//      cmd.index_count    = player_mesh.indices.size();
//      cmd.bone_matrices  = player.bone_matrices;   // float[N][4][4]
//      cmd.bone_count     = player.bone_count;
//      memcpy(cmd.vp, view_proj.data(), 64);        // column-major float[16]
//      cmd.glow_color[0]  = 1.f; cmd.glow_color[1] = 0.4f;
//      cmd.glow_color[2]  = 0.f; cmd.glow_color[3] = 1.f;
//      cmd.glow_thickness = 8.f;
//      cmd.alpha          = 1.f;
//      cmd.player_origin[0] = player.origin.x;
//      cmd.player_origin[1] = player.origin.y;
//      cmd.player_origin[2] = player.origin.z;
//      cmd.screen_w       = g_ow.width;
//      cmd.screen_h       = g_ow.height;
//      cmd.bbox_min_y     = player.bbox_ndc_min_y;
//      cmd.bbox_max_y     = player.bbox_ndc_max_y;
//      GlowRenderer::submit(cmd);
//
//  After all players submitted:
//      GlowRenderer::end_frame();
//
//  Then continue with your ImGui frame as normal.
//
//  In shutdown():
//      GlowRenderer::shutdown();
// ─────────────────────────────────────────────────────────────────────────────