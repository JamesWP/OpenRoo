/* The rendering vocabulary game code speaks to RenderDevice in: render
 * states and their values, primitive types, transforms and vertex formats.
 * Nothing here depends on the backend's headers.
 *
 * The render-state names and their value enums carry Direct3D's numbers,
 * because theme files store blend modes and texture addressing as those
 * numbers and hand them straight through.  Primitive types, transforms and
 * vertex formats are mapped by the backend. */

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

enum class RS : uint32_t {
    ZEnable           = 7,
    ShadeMode         = 9,
    ZWriteEnable      = 14,
    TextureMag        = 17,
    TextureMin        = 18,
    SrcBlend          = 19,
    DestBlend         = 20,
    TextureMapBlend   = 21,
    CullMode          = 22,
    DitherEnable      = 26,
    AlphaBlendEnable  = 27,
    FogEnable         = 28,
    SpecularEnable    = 29,
    FogColor          = 34,
    FogTableMode      = 35,
    FogTableStart     = 36,
    FogTableEnd       = 37,
    FogTableDensity   = 38,
    ColorKeyEnable    = 41,
    TextureAddressU   = 44,
    TextureAddressV   = 45,
    StencilEnable     = 52,
    StencilFail       = 53,
    StencilZFail      = 54,
    StencilPass       = 55,
    StencilFunc       = 56,
    StencilRef        = 57,
    StencilMask       = 58,
    StencilWriteMask  = 59,
    TextureFactor     = 60,
    Ambient           = 139,
};

/* Values for RS::SrcBlend / RS::DestBlend. */
namespace Blend {
enum : uint32_t {
    Zero = 1, One = 2, SrcColor = 3, InvSrcColor = 4, SrcAlpha = 5,
    InvSrcAlpha = 6, DestAlpha = 7, InvDestAlpha = 8, DestColor = 9,
    InvDestColor = 10,
};
}

/* Values for RS::StencilFunc. */
namespace Cmp {
enum : uint32_t {
    Never = 1, Less = 2, Equal = 3, LessEqual = 4, Greater = 5,
    NotEqual = 6, GreaterEqual = 7, Always = 8,
};
}

/* Values for RS::StencilFail / StencilZFail / StencilPass. */
namespace StencilOp {
enum : uint32_t {
    Keep = 1, Zero = 2, Replace = 3, IncrSat = 4, DecrSat = 5, Invert = 6,
    Incr = 7, Decr = 8,
};
}

/* Values for RS::CullMode. */
namespace Cull {
enum : uint32_t { None = 1, CW = 2, CCW = 3 };
}

/* Values for RS::TextureAddressU / V. */
namespace TexAddress {
enum : uint32_t { Wrap = 1, Mirror = 2, Clamp = 3, Border = 4 };
}

/* Values for RS::FogTableMode. */
namespace FogMode {
enum : uint32_t { None = 0, Exp = 1, Exp2 = 2, Linear = 3 };
}

/* Values for RS::ShadeMode. */
namespace ShadeMode {
enum : uint32_t { Flat = 1, Gouraud = 2, Phong = 3 };
}

/* Values for RS::TextureMapBlend. */
namespace TexBlend {
enum : uint32_t {
    Decal = 1, Modulate = 2, DecalAlpha = 3, ModulateAlpha = 4,
    DecalMask = 5, ModulateMask = 6, Copy = 7, Add = 8,
};
}

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
    Diffuse1,   // x,y,z, diffuse, one UV set (strided quads)
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

/* One stream of DrawStrided: a pointer and a byte stride. */
struct VertexStream {
    const void *data;
    uint32_t    stride;
};

/* DrawStrided's inputs: position, normal, diffuse, specular and up to eight
 * texture-coordinate streams. */
struct StridedVertices {
    VertexStream position, normal, diffuse, specular;
    VertexStream texCoords[8];
};
