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
#include "glnative.h"

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

GLuint gl_build_program(char *error, size_t errorSize)
{
    GLuint vs = compile(GL_VERTEX_SHADER, kVertex, error, errorSize);
    GLuint fs = vs ? compile(GL_FRAGMENT_SHADER, kFragment, error, errorSize) : 0;
    if (!vs || !fs) {
        if (vs) gl.DeleteShader(vs);
        return 0;
    }
    GLuint p = gl.CreateProgram();
    gl.AttachShader(p, vs);
    gl.AttachShader(p, fs);
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
    gl.UniformBlockBinding(p, gl.GetUniformBlockIndex(p, "Scene"), 0);
    gl.UniformBlockBinding(p, gl.GetUniformBlockIndex(p, "Draw"), 1);
    gl.UseProgram(p);
    gl.Uniform1i(gl.GetUniformLocation(p, "uTex"), 0);
    return p;
}
