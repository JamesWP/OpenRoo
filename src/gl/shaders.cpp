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
    vec4 color = aColor;
    vec3 specular = aSpecular.rgb * uFlags.z;
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
    const char *sources[3] = { "#version 330 core\n", kBlocks, body };
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
    gl.Uniform1i(gl.GetUniformLocation(p, "uTables"), 1);
    gl.Uniform1i(gl.GetUniformLocation(p, "uShapes"), 1);
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
 * texelFetch, gl_VertexID, instancing and integer uniforms, with no
 * extension.  Each slot is three vec4s (see GLParticleBuffer): position and
 * life (dead below zero), velocity and shape index, and colour in 0..255.
 *
 * The step runs one vertex per slot with the rasteriser off, and writes the
 * slot's next state.  A slot in this step's emission range that is dead takes
 * a new particle from the tables; then every live slot ages, moves through
 * the field, dies or fades. */
static const char *kParticleStep = R"GLSL(
precision highp float;
precision highp int;
precision highp sampler2D;

layout(location = 0) in vec4 aPosLife;
layout(location = 1) in vec4 aVel;
layout(location = 2) in vec4 aColor;

out vec4 oPosLife;
out vec4 oVel;
out vec4 oColor;

uniform sampler2D uTables;  // rows: pos, vel, life, colour
uniform ivec4 uCounts;      // the four tables' lengths
uniform ivec4 uCursor;      // where the first new particle reads each
uniform ivec4 uEmit;        // count, first slot, slots, velocity stride
uniform float uDt;
uniform float uPosScale;
uniform mat4  uPosMatrix;
uniform vec3  uPosOffset;
uniform vec3  uVelOffset;
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

vec4 table(int row, int cursor, int k, int stride, int count)
{
    return texelFetch(uTables, ivec2((cursor + k * stride) % count, row), 0);
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
        vec4 p = uPosMatrix * vec4(table(0, uCursor.x, k, 1, uCounts.x).xyz * uPosScale, 1.0);
        if (p.w != 0.0)
            p.xyz /= p.w;
        pl = vec4(p.xyz + uPosOffset, table(2, uCursor.z, k, 1, uCounts.z).x);
        vel = table(1, uCursor.y, k, uEmit.w, uCounts.y).xyz + uVelOffset;
        col = table(3, uCursor.w, k, 1, uCounts.w);
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

/* The draw: one instance per slot, six vertices each, which the main
 * program's fragment shader finishes as it does the Lit vertices the
 * particles were once drawn as.  A dead slot's vertices are put outside the
 * clip volume. */
static const char *kParticleDraw = R"GLSL(
precision highp float;
precision highp int;
precision highp sampler2D;

layout(location = 0) in vec4 aPosLife;
layout(location = 1) in vec4 aVel;      // .w: the slot's shape
layout(location = 2) in vec4 aColor;

uniform sampler2D uShapes;   // six corners per row
uniform int  uShapeCount;
uniform int  uPerParticle;
uniform vec2 uCornerUV[6];

out vec4  vColor;
out vec3  vSpecular;
out vec2  vUV;
out float vFogDepth;

void main()
{
    vColor = aColor / 255.0;
    vSpecular = vec3(0.0);
    vUV = uCornerUV[gl_VertexID] * uUV.xy + uUV.zw;
    vFogDepth = 0.0;
    if (aPosLife.w < 0.0) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }
    int shape = uPerParticle != 0 ? clamp(int(aVel.w), 0, uShapeCount - 1) : 0;
    vec3 corner = texelFetch(uShapes, ivec2(gl_VertexID, shape), 0).xyz;
    vec4 view = uView * (uWorld * vec4(aPosLife.xyz + corner, 1.0));
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
