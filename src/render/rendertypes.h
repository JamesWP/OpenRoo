/* The rendering vocabulary game code speaks to RenderDevice in: pipeline
 * state, primitive types, vertex formats and the matrices and lights they
 * are drawn with.  Nothing here depends on the backend's headers. */

#pragma once
#include <stdint.h>

struct Mat4 { float m[16]; };   /* row-major, row vectors: v * M */
struct Vec3 { float x, y, z; };

/* A colour with float channels, 0..1. */
struct ColorF { float r, g, b, a; };

/* The surface material lit geometry is drawn with. */
struct Material {
    ColorF diffuse, ambient, specular, emissive;
    float  power;  // specular exponent
};

/* A white-ish light from one direction, at any distance. */
struct DirectionalLight {
    ColorF color;
    Vec3   direction;
};

/* ── Pipeline state ──
 *
 * The device's fixed state comes in a handful of small value types, each set
 * whole.  A backend with immutable pipeline objects can hash one and look it
 * up; the Direct3D 9 backend writes it through.  The defaults are the state a
 * fresh device starts in.
 *
 * The enumerators' numbers are Direct3D's, and nothing relies on that: the
 * theme parser (ui/theme.cpp) names them by keyword, and every backend maps
 * them with a switch. */

enum class BlendFactor : uint8_t {
    Zero = 1, One, SrcColor, InvSrcColor, SrcAlpha, InvSrcAlpha, DestAlpha,
    InvDestAlpha, DestColor, InvDestColor, SrcAlphaSat,
    /* Themes may name this one as a source factor.  It stands for the pair
     * (InvSrcAlpha, SrcAlpha), and the destination factor set with it
     * replaces the second of them. */
    BothInvSrcAlpha = 13,
};

struct BlendState {
    bool        enable = false;
    BlendFactor src    = BlendFactor::One;
    BlendFactor dst    = BlendFactor::Zero;

    static BlendState off() { return BlendState(); }
    static BlendState on(BlendFactor s, BlendFactor d) { return { true, s, d }; }
    /* Straight alpha: what text, the HUD and the menus draw with. */
    static BlendState alpha() { return on(BlendFactor::SrcAlpha, BlendFactor::InvSrcAlpha); }
    static BlendState additive() { return on(BlendFactor::One, BlendFactor::One); }
};

/* Depth test and write.  Both default on. */
struct DepthState {
    bool test  = true;
    bool write = true;
};

enum class CompareFunc : uint8_t {
    Never = 1, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always,
};

enum class StencilOp : uint8_t {
    Keep = 1, Zero, Replace, IncrSat, DecrSat, Invert, Incr, Decr,
};

/* The stencil test, against a reference of `ref`, with every bit of the
 * buffer read and written. */
struct StencilState {
    bool        enable = false;
    CompareFunc func   = CompareFunc::Always;
    uint8_t     ref    = 0;
    StencilOp   fail   = StencilOp::Keep;   // the stencil test failed
    StencilOp   zfail  = StencilOp::Keep;   // the stencil test passed, depth failed
    StencilOp   pass   = StencilOp::Keep;   // both passed
};

/* Which faces are culled.  CW and CCW name the winding that is *kept*. */
enum class CullMode : uint8_t { None = 1, CW, CCW };

struct RasterState {
    CullMode cull = CullMode::CCW;
};

enum class Filter : uint8_t { Nearest = 1, Linear = 2 };
enum class AddressMode : uint8_t { Wrap = 1, Mirror, Clamp, Border };

/* How one texture stage samples. */
struct SamplerState {
    Filter      mag = Filter::Nearest;
    Filter      min = Filter::Nearest;
    AddressMode u   = AddressMode::Wrap;
    AddressMode v   = AddressMode::Wrap;
};

enum class FogMode : uint8_t { None = 0, Exp = 1, Exp2 = 2, Linear = 3 };

/* Theme files and the extra-object tables hold blend factors and texture
 * address modes as numbers (the BlendFactor and AddressMode enumerators'),
 * with 0 for "not set".  These make states of them: blending is on only when
 * both factors are set; an unset address clamps; a number that names nothing
 * is taken as the plainest choice. */
inline BlendFactor blendFactorFromTheme(uint32_t v)
{
    return (v >= 1 && v <= 11) || v == 13 ? (BlendFactor)v : BlendFactor::One;
}
inline BlendState blendFromTheme(uint32_t src, uint32_t dst)
{
    return src != 0 && dst != 0
        ? BlendState::on(blendFactorFromTheme(src), blendFactorFromTheme(dst))
        : BlendState::off();
}
inline AddressMode addressFromTheme(uint32_t v)
{
    return v >= 1 && v <= 4 ? (AddressMode)v : AddressMode::Clamp;
}

/* Per-pixel fog.  `start` and `end` are for Linear, `density` for Exp and
 * Exp2; the colour is 0x00RRGGBB. */
struct FogState {
    bool     enable  = false;
    FogMode  mode    = FogMode::None;
    uint32_t color   = 0;
    float    start   = 0.0f;
    float    end     = 1.0f;
    float    density = 1.0f;
};

enum class Prim {
    PointList, LineList, LineStrip, TriangleList, TriangleStrip, TriangleFan,
};

enum class Transform { World, View, Projection };

/* The vertex layouts the game draws with.  Each struct below is one of
 * them. */
enum class VertexFormat {
    Screen,     // ScreenVertex: pre-transformed, lit
    Lit,        // LitVertex: world space, lit, one UV set
    Normal2,    // x,y,z, normal, two UV sets (40 bytes; meshes)
    Diffuse1,   // Diffuse1Vertex: x,y,z, diffuse, one UV set (scene quads)
    Diffuse2,   // x,y,z, diffuse, two UV sets (36 bytes; quads)
};

/* Screen-space: position in pixels, 1/w, colours, one UV set. */
struct ScreenVertex {
    float    sx, sy, sz, rhw;
    uint32_t color, specular;
    float    tu, tv;
};

/* World space, already lit: position, a reserved word, colours, one UV
 * set. */
struct LitVertex {
    float    x, y, z;
    uint32_t reserved;
    uint32_t color, specular;
    float    tu, tv;
};

/* VertexFormat::Diffuse1: position, a diffuse colour, one UV set. */
struct Diffuse1Vertex {
    float    x, y, z;
    uint32_t diffuse;
    float    tu, tv;
};
