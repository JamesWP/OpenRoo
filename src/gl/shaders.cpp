/* The one GLSL program every draw uses.  It does what the fixed-function
 * pipeline the game was written for did, from the game's state:
 *
 *   vertex    transforms by world, view and projection -- or, for Screen
 *             vertices, maps pixel positions to clip space -- then lights
 *             vertices that have normals with the one directional light, per
 *             vertex (Gouraud, as before);
 *   fragment  modulates the vertex colour by the texture (whose alpha, if the
 *             texture has one, replaces the vertex's), adds the specular
 *             colour, and fogs.
 *
 * Two conventions of Direct3D, which the game's vertices and matrices still
 * follow, are mapped here:
 *   - clip-space depth runs 0..w, not -w..w: `z = 2z - w` after the
 *     projection;
 *   - pixel centres are on whole numbers, not half ones: everything is moved
 *     half a pixel right and down, so a quad that covered pixels 0..n-1
 *     still does, and a texel lands on a pixel.
 * Fog is a function of the distance along the view axis (the "w-fog" the
 * game's themes were tuned with); for Screen vertices, of their depth. */

#include <stdio.h>
#include <string.h>
#include "glnative_internal.h"

static const char *kBlocks = R"GLSL(
layout(std140) uniform Scene {
    mat4 uView;
    mat4 uProj;
    vec4 uEye;
    vec4 uLightDir;
    vec4 uLightDiffuse;
    vec4 uLightSpecular;
    vec4 uAmbient;
    vec4 uFogColor;
    vec4 uFog;       // start, end, density, mode
    vec4 uTarget;    // 1/width, -1/height, width, height
};
layout(std140) uniform Draw {
    mat4 uWorld;
    mat4 uNormal;
    vec4 uMatDiffuse;
    vec4 uMatAmbient;
    vec4 uMatSpecular;  // .w: power
    vec4 uMatEmissive;
    vec4 uUV;           // scaleU, scaleV, offsetU, offsetV
    vec4 uFlags;        // screen, lit, specular, texture mode (0 none, 1 rgb, 2 rgba)
};
)GLSL";

static const char *kVertex = R"GLSL(
layout(location = 0) in vec4 aPos;       // xyz; Screen vertices also 1/w
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 aSpecular;
layout(location = 4) in vec2 aUV;

out vec4  vColor;
out vec3  vSpecular;
out vec2  vUV;
out float vFogDepth;

void main()
{
    vec4 color = aColor.bgra;  // D3DCOLOR bytes, read as RGBA
    vec3 specular = aSpecular.bgr * uFlags.z;
    vec2 uv = aUV;
    float fogDepth;

    if (uFlags.x > 0.5) {
        // Pixel position, depth and 1/w, already transformed.
        float w = aPos.w == 0.0 ? 1.0 : 1.0 / aPos.w;
        vec3 ndc = vec3(aPos.x / uTarget.z * 2.0 - 1.0,
                        1.0 - aPos.y / uTarget.w * 2.0,
                        aPos.z * 2.0 - 1.0);
        gl_Position = vec4(ndc * w, w);
        fogDepth = aPos.z;
    } else {
        vec4 world = uWorld * vec4(aPos.xyz, 1.0);
        vec4 view  = uView * world;
        gl_Position = uProj * view;
        gl_Position.z = gl_Position.z * 2.0 - gl_Position.w;
        fogDepth = view.z;
        uv = uv * uUV.xy + uUV.zw;

        if (uFlags.y > 0.5) {
            vec3 n = normalize(mat3(uNormal) * aNormal);
            vec3 l = -uLightDir.xyz;
            float ndl = max(dot(n, l), 0.0);
            vec3 lit = uMatEmissive.rgb
                     + uMatAmbient.rgb * uAmbient.rgb
                     + uMatDiffuse.rgb * uLightDiffuse.rgb * ndl;
            color = vec4(clamp(lit, 0.0, 1.0), clamp(uMatDiffuse.a, 0.0, 1.0));
            specular = vec3(0.0);
            if (uFlags.z > 0.5 && ndl > 0.0) {
                vec3 h = normalize(l + normalize(uEye.xyz - world.xyz));
                float s = pow(max(dot(n, h), 0.0), uMatSpecular.w);
                specular = clamp(uMatSpecular.rgb * uLightSpecular.rgb * s, 0.0, 1.0);
            }
        }
    }
    gl_Position.xy += uTarget.xy * gl_Position.w;
    gl_PointSize = 1.0;  // ES needs it; core ignores it (its default is 1)

    vColor = color;
    vSpecular = specular;
    vUV = uv;
    vFogDepth = fogDepth;
}
)GLSL";

static const char *kFragment = R"GLSL(
uniform sampler2D uTex;

in vec4  vColor;
in vec3  vSpecular;
in vec2  vUV;
in float vFogDepth;
out vec4 oColor;

void main()
{
    vec4 c = vColor;
    if (uFlags.w > 0.5) {
        vec4 t = texture(uTex, vUV);
        c.rgb *= t.rgb;
        if (uFlags.w > 1.5)
            c.a = t.a;
    }
    c.rgb += vSpecular;

    int mode = int(uFog.w + 0.5);
    if (mode != 0) {
        float d = abs(vFogDepth);
        float f;
        if (mode == 3)
            f = (uFog.y - d) / max(uFog.y - uFog.x, 1e-6);
        else if (mode == 1)
            f = exp(-d * uFog.z);
        else
            f = exp(-(d * uFog.z) * (d * uFog.z));
        c.rgb = mix(uFogColor.rgb, c.rgb, clamp(f, 0.0, 1.0));
    }
    oColor = c;
}
)GLSL";

static GLuint compile(GLenum type, const char *body, char *error, size_t errorSize)
{
    const char *version = gl_flavour == GLFlavour::ES32
        ? "#version 320 es\nprecision highp float;\nprecision highp int;\n"
        : "#version 330 core\n";
    const char *sources[3] = { version, kBlocks, body };
    GLuint s = gl.CreateShader(type);
    gl.ShaderSource(s, 3, sources, NULL);
    gl.CompileShader(s);
    GLint ok = 0;
    gl.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[900] = "";
        gl.GetShaderInfoLog(s, sizeof(log), NULL, log);
        snprintf(error, errorSize, "The %s shader did not compile: %s",
                 type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        gl.DeleteShader(s);
        return 0;
    }
    return s;
}

/* Links vs and fs (which it deletes) into a program, capturing `varyings`
 * interleaved by transform feedback if there are any.  The program has its
 * uniform blocks bound and its samplers on their units. */
static GLuint link(GLuint vs, GLuint fs, const char *const *varyings, int nVaryings,
                   char *error, size_t errorSize)
{
    if (!vs || !fs) {
        if (vs) gl.DeleteShader(vs);
        if (fs) gl.DeleteShader(fs);
        return 0;
    }
    GLuint p = gl.CreateProgram();
    gl.AttachShader(p, vs);
    gl.AttachShader(p, fs);
    if (nVaryings)
        gl.TransformFeedbackVaryings(p, nVaryings, varyings, GL_INTERLEAVED_ATTRIBS);
    gl.LinkProgram(p);
    gl.DeleteShader(vs);
    gl.DeleteShader(fs);
    GLint ok = 0;
    gl.GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[900] = "";
        gl.GetProgramInfoLog(p, sizeof(log), NULL, log);
        snprintf(error, errorSize, "The shader program did not link: %s", log);
        gl.DeleteProgram(p);
        return 0;
    }
    // A program that uses neither block (the particle step) has it
    // optimised away.
    const char *blocks[2] = { "Scene", "Draw" };
    for (GLuint b = 0; b < 2; b++) {
        const GLuint index = gl.GetUniformBlockIndex(p, blocks[b]);
        if (index != GL_INVALID_INDEX)
            gl.UniformBlockBinding(p, index, b);
    }
    gl.UseProgram(p);
    gl.Uniform1i(gl.GetUniformLocation(p, "uTex"), 0);
    gl.Uniform1i(gl.GetUniformLocation(p, "uSlots"), 1);
    gl.Uniform1i(gl.GetUniformLocation(p, "uShapes"), 2);
    gl.Uniform1i(gl.GetUniformLocation(p, "uShards"), 1);
    return p;
}

GLuint gl_build_program(char *error, size_t errorSize)
{
    GLuint vs = compile(GL_VERTEX_SHADER, kVertex, error, errorSize);
    GLuint fs = vs ? compile(GL_FRAGMENT_SHADER, kFragment, error, errorSize) : 0;
    return link(vs, fs, NULL, 0, error, errorSize);
}

/* ── Particles ──
 *
 * Both stay inside what OpenGL ES 3.0 has as well: transform feedback,
 * texelFetch, gl_VertexID, instancing, integer and unsigned arithmetic, with
 * no extension.  Each slot is three vec4s (see GLParticleBuffer): position and
 * life (dead below zero), velocity and shape index, and colour in 0..255.
 *
 * The step runs one vertex per slot with the rasteriser off, and writes the
 * slot's next state.  A slot in this step's emission range that is dead takes
 * a new particle, drawn from a hash of the slot and the step's seed; then
 * every live slot ages, moves through the field, dies or fades. */
static const char *kParticleStep = R"GLSL(
precision highp float;
precision highp int;

layout(location = 0) in vec4 aPosLife;
layout(location = 1) in vec4 aVel;
layout(location = 2) in vec4 aColor;

out vec4 oPosLife;
out vec4 oVel;
out vec4 oColor;

uniform ivec4 uEmit;        // count, first slot, slots, seed
uniform int   uSpawn;       // 0 gauss, 1 box, 2 circle
uniform vec3  uPosA;
uniform vec3  uPosB;
uniform float uPosScale;
uniform mat4  uPosMatrix;
uniform vec3  uPosOffset;
uniform vec3  uDirMean;
uniform vec3  uDirVariance;
uniform vec2  uSpeed;       // mean, variance
uniform vec3  uVelOffset;
uniform vec2  uLife;        // mean, variance
uniform int   uColourCount;
uniform vec4  uColours[8];  // 0..255
uniform float uWeights[8];
uniform float uDt;
uniform int   uField;       // 0 none, 1 gravity, 2 magnet
uniform vec3  uAccel;
uniform vec3  uClipOn;
uniform vec3  uClipMin;
uniform vec3  uClipMax;
uniform vec3  uCentre;
uniform vec3  uForce;
uniform vec3  uHalfExtent;
uniform vec3  uFadeRGB;
uniform float uFadeStep;

uint hash(uint v)
{
    uint s = v * 747796405u + 2891336453u;
    uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}

uint g_rng;

float uniform01()
{
    g_rng = hash(g_rng);
    return float(g_rng >> 8u) * (1.0 / 16777216.0);
}

float gauss(float mean, float variance)
{
    if (!(variance > 0.0))
        return mean;
    float z = 0.0;
    for (int i = 0; i < 8; i++) {
        float r = sqrt(-2.0 * log(1.0 - uniform01()));
        z = r * cos(6.2831853 * uniform01());
        if (abs(z) <= 2.45)
            break;
    }
    return mean + clamp(z, -2.45, 2.45) * sqrt(variance);
}

vec3 gauss3(vec3 mean, vec3 variance)
{
    return vec3(gauss(mean.x, variance.x), gauss(mean.y, variance.y),
                gauss(mean.z, variance.z));
}

vec4 colour()
{
    float total = 0.0;
    for (int i = 0; i < uColourCount; i++)
        total += uWeights[i];
    if (uColourCount == 0 || !(total > 0.0))
        return vec4(255.0);
    float pick = uniform01() * total;
    for (int i = 0; i < uColourCount; i++) {
        pick -= uWeights[i];
        if (pick < 0.0)
            return uColours[i];
    }
    return uColours[uColourCount - 1];
}

void main()
{
    vec4 pl = aPosLife;
    vec3 vel = aVel.xyz;
    vec4 col = aColor;

    int k = gl_VertexID - uEmit.y;
    if (k < 0)
        k += uEmit.z;
    if (k < uEmit.x && pl.w < 0.0) {
        g_rng = hash(uint(gl_VertexID) ^ hash(uint(uEmit.w)));
        vec3 p;
        if (uSpawn == 1) {
            p = mix(uPosA, uPosB, vec3(uniform01(), uniform01(), uniform01()));
        } else if (uSpawn == 2) {
            float a = 6.2831853 * uniform01();
            p = vec3(sin(a), 0.0, cos(a));
        } else {
            p = gauss3(uPosA, uPosB);
        }
        vec4 t = uPosMatrix * vec4(p * uPosScale, 1.0);
        if (t.w != 0.0)
            t.xyz /= t.w;
        vec3 dir = gauss3(uDirMean, uDirVariance);
        if (length(dir) > 0.0)
            dir = normalize(dir);
        vel = dir * gauss(uSpeed.x, uSpeed.y) + uVelOffset;
        pl = vec4(t.xyz + uPosOffset, gauss(uLife.x, uLife.y));
        col = colour();
    }

    if (uField != 0 && pl.w >= 0.0) {
        pl.w -= uDt;
        bool dies = pl.w < 0.0;
        if (!dies && uField == 1) {
            vel += uAccel * uDt;
            pl.xyz += vel * uDt;
            for (int a = 0; a < 3; a++)
                if (uClipOn[a] > 0.5 && (pl[a] > uClipMax[a] || pl[a] < uClipMin[a]))
                    dies = true;
        } else if (!dies) {
            vec3 d = uCentre - pl.xyz;
            if (all(lessThanEqual(abs(d), uHalfExtent))) {
                dies = true;  // it has reached the magnet
            } else {
                vel += normalize(d) * uForce * uDt;
                pl.xyz += vel * uDt;
            }
        }
        if (dies) {
            pl.w = -1.0;
        } else if (uFadeStep > 0.0) {
            vec3 c = col.rgb;
            col.rgb = mix(max(c - uFadeStep, uFadeRGB), min(c + uFadeStep, uFadeRGB),
                          vec3(lessThan(c, uFadeRGB)));
        }
    }

    oPosLife = pl;
    oVel = vec4(vel, aVel.w);
    oColor = col;
}
)GLSL";

/* OpenGL ES links no program without one, though nothing reaches it. */
static const char *kParticleStepFragment = R"GLSL(
precision mediump float;
out vec4 oColor;
void main() { oColor = vec4(0.0); }
)GLSL";

/* The draw: one instance per placement, six vertices per slot.  The slots
 * are read from the texture the step copies them into (PARTICLE_ROW_SLOTS slots
 * of three texels to a row), the placement's matrices from instanced
 * attributes; the main program's fragment shader finishes it.  A dead slot's
 * vertices are put outside the clip volume. */
static const char *kParticleDraw = R"GLSL(
precision highp float;
precision highp int;
precision highp sampler2D;

layout(location = 3) in vec4 aWorld0;   // the placement's world, by rows
layout(location = 4) in vec4 aWorld1;
layout(location = 5) in vec4 aWorld2;
layout(location = 6) in vec4 aWorld3;
layout(location = 7) in vec4 aCorner0;  // and its corner transform
layout(location = 8) in vec4 aCorner1;
layout(location = 9) in vec4 aCorner2;
layout(location = 10) in vec4 aCorner3;

uniform sampler2D uSlots;
uniform sampler2D uShapes;   // seven texels a shape: six corners, axis and rate
uniform int   uShapeCount;
uniform int   uRowSlots;
uniform int   uTumbling;
uniform vec3  uFacing;
uniform float uSize;
uniform float uTime;
uniform vec2  uCornerUV[6];

out vec4  vColor;
out vec3  vSpecular;
out vec2  vUV;
out float vFogDepth;

/* The six corners of a quad `size` across, square to `facing`. */
vec3 billboard(int corner)
{
    vec3 basis = uFacing == vec3(1.0, 0.0, 0.0) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 d = normalize(uFacing);
    vec3 e2 = cross(d, basis);
    if (length(e2) < 1e-6)
        e2 = cross(d, vec3(0.0, 1.0, 0.0));
    vec3 e1 = cross(d, e2);
    e1 *= uSize / length(e1);
    e2 *= uSize / length(e2);
    vec3 c = corner == 1 || corner == 4 ? e1
           : corner == 2 || corner == 3 ? e2
           : corner == 5 ? e1 + e2 : vec3(0.0);
    return c - (e1 + e2) * 0.5;
}

/* Corner `corner` of shape `shape`, turned for uTime. */
vec3 tumbled(int shape, int corner)
{
    vec3 v = texelFetch(uShapes, ivec2(shape * 7 + corner, 0), 0).xyz;
    vec4 spin = texelFetch(uShapes, ivec2(shape * 7 + 6, 0), 0);
    float a = spin.w * uTime;
    vec3 k = spin.xyz;
    return v * cos(a) + cross(k, v) * sin(a) + k * dot(k, v) * (1.0 - cos(a));
}

void main()
{
    int slot = gl_VertexID / 6;
    int corner = gl_VertexID - slot * 6;
    ivec2 at = ivec2((slot % uRowSlots) * 3, slot / uRowSlots);
    vec4 posLife = texelFetch(uSlots, at, 0);
    vec4 vel = texelFetch(uSlots, at + ivec2(1, 0), 0);
    vec4 color = texelFetch(uSlots, at + ivec2(2, 0), 0);

    vColor = color / 255.0;
    vSpecular = vec3(0.0);
    vUV = uCornerUV[corner] * uUV.xy + uUV.zw;
    vFogDepth = 0.0;
    if (posLife.w < 0.0) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }

    vec3 offset;
    if (uTumbling != 0) {
        offset = tumbled(clamp(int(vel.w), 0, uShapeCount - 1), corner);
    } else {
        vec4 c = mat4(aCorner0, aCorner1, aCorner2, aCorner3) * vec4(billboard(corner), 1.0);
        offset = c.w != 0.0 ? c.xyz / c.w : c.xyz;
    }
    mat4 world = mat4(aWorld0, aWorld1, aWorld2, aWorld3);
    vec4 view = uView * (world * vec4(posLife.xyz + offset, 1.0));
    gl_Position = uProj * view;
    gl_Position.z = gl_Position.z * 2.0 - gl_Position.w;
    gl_Position.xy += uTarget.xy * gl_Position.w;
    vFogDepth = view.z;
}
)GLSL";

bool gl_build_particle_programs(GLuint *sim, GLuint *draw, char *error, size_t errorSize)
{
    static const char *const varyings[3] = { "oPosLife", "oVel", "oColor" };
    *sim = link(compile(GL_VERTEX_SHADER, kParticleStep, error, errorSize),
                compile(GL_FRAGMENT_SHADER, kParticleStepFragment, error, errorSize),
                varyings, 3, error, errorSize);
    *draw = *sim ? link(compile(GL_VERTEX_SHADER, kParticleDraw, error, errorSize),
                        compile(GL_FRAGMENT_SHADER, kFragment, error, errorSize),
                        NULL, 0, error, errorSize)
                 : 0;
    if (!*draw && *sim) {
        gl.DeleteProgram(*sim);
        *sim = 0;
    }
    return *sim != 0;
}

/* ── Shattered meshes ──
 *
 * One vertex per mesh vertex, with no attributes: vertex i reads itself and
 * the other two corners of its triangle (i / 3) from the ShardBuffer's
 * texture, and everything else about its shard is drawn from a hash of the
 * triangle and the burst's seed, in a fixed order -- so a shard is the same
 * shard at every time it is drawn.  A gone shard's vertices are put outside
 * the clip volume.  The main program's fragment shader finishes it, as it
 * does a Normal2 vertex drawn with NoLight: white, no specular. */
static const char *kShardDraw = R"GLSL(
precision highp float;
precision highp int;
precision highp sampler2D;

uniform sampler2D uShards;   // two texels a vertex: position and U, normal and V
uniform int   uRowVertices;
uniform vec3  uOrigin;
uniform vec2  uSpeed;        // mean, variance
uniform float uSpin;
uniform vec2  uDrop;         // seconds the last shard can last (0: forever), shrink
uniform int   uSeed;
uniform float uTime;

out vec4  vColor;
out vec3  vSpecular;
out vec2  vUV;
out float vFogDepth;

uint hash(uint v)
{
    uint s = v * 747796405u + 2891336453u;
    uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}

uint g_rng;

float uniform01()
{
    g_rng = hash(g_rng);
    return float(g_rng >> 8u) * (1.0 / 16777216.0);
}

float gauss(float mean, float variance)
{
    if (!(variance > 0.0))
        return mean;
    float z = 0.0;
    for (int i = 0; i < 8; i++) {
        float r = sqrt(-2.0 * log(1.0 - uniform01()));
        z = r * cos(6.2831853 * uniform01());
        if (abs(z) <= 2.45)
            break;
    }
    return mean + clamp(z, -2.45, 2.45) * sqrt(variance);
}

vec3 unitVector()
{
    float z = uniform01() * 2.0 - 1.0;
    float a = 6.2831853 * uniform01();
    float r = sqrt(max(1.0 - z * z, 0.0));
    return vec3(r * cos(a), r * sin(a), z);
}

vec4 shardTexel(int vertex, int which)
{
    return texelFetch(uShards, ivec2((vertex % uRowVertices) * 2 + which,
                                     vertex / uRowVertices), 0);
}

void main()
{
    int tri = gl_VertexID / 3;
    vec4 me = shardTexel(gl_VertexID, 0);
    float v = shardTexel(gl_VertexID, 1).w;
    vec3 centre = (shardTexel(tri * 3, 0).xyz + shardTexel(tri * 3 + 1, 0).xyz
                   + shardTexel(tri * 3 + 2, 0).xyz) / 3.0;

    g_rng = hash(uint(tri) ^ hash(uint(uSeed)));
    float speed = gauss(uSpeed.x, uSpeed.y);
    vec3 axis = unitVector();
    float angle = uSpin * (uniform01() * 2.0 - 1.0) * uTime;
    float lasts = uniform01() * uDrop.x;
    vec3 stray = unitVector();

    vColor = vec4(1.0);
    vSpecular = vec3(0.0);
    vUV = vec2(me.w, v) * uUV.xy + uUV.zw;
    vFogDepth = 0.0;

    float scale = 1.0;
    if (uDrop.x > 0.0) {
        float left = lasts - uTime;
        if (left <= 0.0) {
            gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
            return;
        }
        if (uDrop.y > 0.0)
            scale = min(left / uDrop.y, 1.0);
    }

    // Out from the origin through the centroid; one on the origin strays.
    vec3 out_ = centre - uOrigin;
    out_ = dot(out_, out_) > 1e-12 ? normalize(out_) : stray;
    vec3 c = (me.xyz - centre) * scale;
    vec3 turned = c * cos(angle) + cross(axis, c) * sin(angle)
                + axis * dot(axis, c) * (1.0 - cos(angle));
    vec3 pos = centre + out_ * speed * uTime + turned;

    vec4 view = uView * (uWorld * vec4(pos, 1.0));
    gl_Position = uProj * view;
    gl_Position.z = gl_Position.z * 2.0 - gl_Position.w;
    gl_Position.xy += uTarget.xy * gl_Position.w;
    vFogDepth = view.z;
}
)GLSL";

GLuint gl_build_shard_program(char *error, size_t errorSize)
{
    GLuint vs = compile(GL_VERTEX_SHADER, kShardDraw, error, errorSize);
    GLuint fs = vs ? compile(GL_FRAGMENT_SHADER, kFragment, error, errorSize) : 0;
    return link(vs, fs, NULL, 0, error, errorSize);
}
