/* RenderDevice's particles (particletypes.h): the buffers and the instanced
 * draw.  The program is in shaders.cpp.
 *
 * Texture unit 2 carries a buffer's shapes, and unit 0, the game's, is left
 * as it was and active.  The draw has a vertex array of its own, so the main
 * one's attributes and divisors are never disturbed. */

#include "glnative_internal.h"
#include "logger.h"
#include <string.h>
#include <algorithm>
#include <vector>

void gl_create_particle_objects(GLRenderDevice::Native *n)
{
    char error[900] = "";
    if (!gl_build_particle_program(&n->particleDraw, error, sizeof(error)))
        g_logger.write("renderdevice: no particles: %s\n", error);
    gl.UseProgram(n->program);
    if (!n->particleDraw)
        return;
    gl.GenVertexArrays(1, &n->particleDrawVao);
    gl.GenBuffers(1, &n->particleInstances);

    // The draw's per-placement matrices, eight vec4s, and its seed.
    gl.BindVertexArray(n->particleDrawVao);
    gl.BindBuffer(GL_ARRAY_BUFFER, n->particleInstances);
    for (GLuint a = 0; a < 8; a++) {
        gl.EnableVertexAttribArray(3 + a);
        gl.VertexAttribPointer(3 + a, 4, GL_FLOAT, GL_FALSE, sizeof(ParticleInstance),
                               (const void *)(size_t)(a * 4 * sizeof(float)));
        gl.VertexAttribDivisor(3 + a, 1);
    }
    gl.EnableVertexAttribArray(11);
    gl.VertexAttribIPointer(11, 1, GL_UNSIGNED_INT, sizeof(ParticleInstance),
                            (const void *)offsetof(ParticleInstance, seed));
    gl.VertexAttribDivisor(11, 1);
    gl.BindVertexArray(n->vao);
}

void gl_release_particle_objects(GLRenderDevice::Native *n)
{
    if (n->particleDraw) {
        gl.DeleteVertexArrays(1, &n->particleDrawVao);
        gl.DeleteBuffers(1, &n->particleInstances);
        gl.DeleteProgram(n->particleDraw);
    }
}

/* A float texture on `unit`, sampled only by texelFetch. */
static void bind_float_texture(GLenum unit, GLuint tex)
{
    gl.ActiveTexture(unit);
    gl.BindTexture(GL_TEXTURE_2D, tex);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

GLParticleBuffer::~GLParticleBuffer()
{
    if (shapes && gl_usable())
        gl.DeleteTextures(1, &shapes);
}

ParticleBuffer *GLRenderDevice::CreateParticleBuffer(uint32_t count)
{
    Native *n = native_;
    if (count == 0)
        return NULL;
    GLParticleBuffer *pb = new GLParticleBuffer();
    pb->count = count;
    if (!n->active || !n->particleDraw)
        return pb;
    gl.GenTextures(1, &pb->shapes);
    return pb;
}

void GLRenderDevice::SetParticleShapes(ParticleBuffer *buf, const ParticleShape *shapes,
                                       uint32_t count)
{
    GLParticleBuffer *pb = static_cast<GLParticleBuffer *>(buf);
    if (!pb || !pb->shapes || !gl_usable() || count == 0)
        return;
    std::vector<float> texels((size_t)count * 7 * 4, 0.0f);
    for (uint32_t s = 0; s < count; s++) {
        float *t = &texels[(size_t)s * 7 * 4];
        for (int c = 0; c < 6; c++)
            memcpy(&t[c * 4], shapes[s].corners[c], 3 * sizeof(float));
        t[24] = shapes[s].axis.x;
        t[25] = shapes[s].axis.y;
        t[26] = shapes[s].axis.z;
        t[27] = shapes[s].rate;
    }
    pb->shapeCount = count;
    bind_float_texture(GL_TEXTURE2, pb->shapes);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, (GLsizei)(count * 7), 1, 0, GL_RGBA, GL_FLOAT,
                  texels.data());
    gl.ActiveTexture(GL_TEXTURE0);
}

/* The emission, the field and the path, as the program's uniforms. */
static void set_emit_uniforms(GLuint p, const ParticleEmit &e, const ParticleField &f)
{
    auto loc = [p](const char *name) { return gl.GetUniformLocation(p, name); };
    auto vec3 = [&](const char *name, const Vec3 &v) {
        const float a[3] = { v.x, v.y, v.z };
        gl.Uniform3fv(loc(name), 1, a);
    };

    gl.Uniform1i(loc("uNewest"), e.newest);
    gl.Uniform1f(loc("uNewestAge"), e.newestAge);
    gl.Uniform1f(loc("uInterval"), e.interval);
    gl.Uniform1i(loc("uSpawn"), (GLint)e.spawn);
    vec3("uPosA", e.posA);
    vec3("uPosB", e.posB);
    gl.Uniform1f(loc("uPosScale"), e.posScale);
    // Row-major with row vectors is column-major with column vectors.
    gl.UniformMatrix4fv(loc("uPosMatrix"), 1, GL_FALSE, e.posMatrix.m);
    vec3("uPosOffset", e.posOffset);
    vec3("uDirMean", e.dirMean);
    vec3("uDirVariance", e.dirVariance);
    const float speed[2] = { e.speedMean, e.speedVariance };
    gl.Uniform2fv(loc("uSpeed"), 1, speed);
    vec3("uVelOffset", e.velOffset);
    const float life[2] = { e.lifeMean, e.lifeVariance };
    gl.Uniform2fv(loc("uLife"), 1, life);
    const uint32_t colours = std::min(e.colourCount, PARTICLE_COLOURS);
    float rgba[PARTICLE_COLOURS][4] = {};
    for (uint32_t i = 0; i < colours; i++) {
        const uint32_t argb = e.colours[i];
        rgba[i][0] = (float)((argb >> 16) & 255);
        rgba[i][1] = (float)((argb >> 8) & 255);
        rgba[i][2] = (float)(argb & 255);
        rgba[i][3] = (float)(argb >> 24);
    }
    gl.Uniform1i(loc("uColourCount"), (GLint)colours);
    gl.Uniform4fv(loc("uColours"), PARTICLE_COLOURS, &rgba[0][0]);
    gl.Uniform1fv(loc("uWeights"), PARTICLE_COLOURS, e.weights);

    const uint32_t samples = e.path ? std::min(e.path->count, PARTICLE_PATH) : 0;
    gl.Uniform1i(loc("uPathCount"), (GLint)samples);
    if (samples) {
        float pos[PARTICLE_PATH][4], dir[PARTICLE_PATH][4];
        for (uint32_t i = 0; i < samples; i++) {
            const ParticlePath &pa = *e.path;
            pos[i][0] = pa.pos[i].x;  pos[i][1] = pa.pos[i].y;  pos[i][2] = pa.pos[i].z;
            pos[i][3] = pa.age[i];
            dir[i][0] = pa.dir[i].x;  dir[i][1] = pa.dir[i].y;  dir[i][2] = pa.dir[i].z;
            dir[i][3] = pa.on[i] ? 1.0f : 0.0f;
        }
        gl.Uniform4fv(loc("uPathPos"), (GLsizei)samples, &pos[0][0]);
        gl.Uniform4fv(loc("uPathDir"), (GLsizei)samples, &dir[0][0]);
    }

    gl.Uniform1i(loc("uField"), (GLint)f.kind);
    vec3("uAccel", f.accel);
    vec3("uClipOn", { f.clip[0] ? 1.0f : 0.0f, f.clip[1] ? 1.0f : 0.0f, f.clip[2] ? 1.0f : 0.0f });
    vec3("uClipMin", f.clipMin);
    vec3("uClipMax", f.clipMax);
    vec3("uCentre", f.centre);
    vec3("uForce", f.force);
    vec3("uHalfExtent", f.halfExtent);
    vec3("uFadeRGB", { (float)((f.fadeRGB >> 16) & 255), (float)((f.fadeRGB >> 8) & 255),
                       (float)(f.fadeRGB & 255) });
    gl.Uniform1f(loc("uFadeRate"), f.fadeRate);
}

bool GLRenderDevice::DrawParticles(const ParticleBuffer *buf, const ParticleEmit &emit,
                                   const ParticleField &field, const ParticleLook &look,
                                   const ParticleInstance *instances, uint32_t count)
{
    const GLParticleBuffer *pb = static_cast<const GLParticleBuffer *>(buf);
    Native *n = native_;
    if (!n->active || gl_fx_nodraw())
        return true;
    if (!pb || !n->particleDraw || count == 0 || (look.tumbling && pb->shapeCount == 0))
        return false;
    if (emit.newest < 0)
        return true;

    gl_prepare_draw(n, state_, (float)((double)mode_->dwWidth / (double)mode_->dwHeight),
                    VertexFormat::Lit, 0);
    // Two-sided while tumbling: a shape turned past edge-on still shows.
    if (look.tumbling && state_.raster.cull != CullMode::None) {
        gl.Disable(GL_CULL_FACE);
        n->dirty |= GLDirty::Raster;
    }

    bind_float_texture(GL_TEXTURE2, pb->shapes);
    gl.ActiveTexture(GL_TEXTURE0);

    const GLuint p = n->particleDraw;
    auto loc = [p](const char *name) { return gl.GetUniformLocation(p, name); };
    gl.UseProgram(p);
    set_emit_uniforms(p, emit, field);
    gl.Uniform1i(loc("uShapeCount"), (GLint)pb->shapeCount);
    gl.Uniform1i(loc("uTumbling"), look.tumbling ? 1 : 0);
    const float facing[3] = { look.facing.x, look.facing.y, look.facing.z };
    gl.Uniform3fv(loc("uFacing"), 1, facing);
    gl.Uniform1f(loc("uSize"), look.size);
    gl.Uniform2fv(loc("uCornerUV"), 6, &look.uv[0][0]);

    // Only as many as have been emitted.
    const uint32_t shown = std::min(pb->count, (uint32_t)emit.newest + 1);
    gl.BindVertexArray(n->particleDrawVao);
    gl.BindBuffer(GL_ARRAY_BUFFER, n->particleInstances);
    gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(count * sizeof(ParticleInstance)), instances,
                  GL_STREAM_DRAW);
    gl.DrawArraysInstanced(GL_TRIANGLES, 0, (GLsizei)(shown * 6), (GLsizei)count);

    gl.BindVertexArray(n->vao);
    gl.UseProgram(n->program);
    return true;
}
