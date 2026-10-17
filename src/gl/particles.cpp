/* RenderDevice's particles (particletypes.h): the buffers, the step by
 * transform feedback and the instanced draw.  The programs are in
 * shaders.cpp.
 *
 * Texture units 1 and 2 carry the particle textures -- a buffer's slots and
 * its shapes -- and unit 0, the game's, is left as it was and active.  Each
 * pass has a vertex array of its own, so the main one's attributes and
 * divisors are never disturbed. */

#include "glnative_internal.h"
#include "logger.h"
#include <string.h>
#include <algorithm>
#include <vector>

static const GLsizei kSlotStride = 12 * sizeof(float);

void gl_create_particle_objects(GLRenderDevice::Native *n)
{
    char error[900] = "";
    if (!gl_build_particle_programs(&n->particleSim, &n->particleDraw, error, sizeof(error)))
        g_logger.write("renderdevice: no particles: %s\n", error);
    gl.UseProgram(n->program);
    if (!n->particleSim)
        return;
    GLuint vaos[2];
    gl.GenVertexArrays(2, vaos);
    n->particleSimVao = vaos[0];
    n->particleDrawVao = vaos[1];
    gl.GenBuffers(1, &n->particleInstances);

    // The draw's per-placement matrices: eight vec4s an instance.
    gl.BindVertexArray(n->particleDrawVao);
    gl.BindBuffer(GL_ARRAY_BUFFER, n->particleInstances);
    for (GLuint a = 0; a < 8; a++) {
        gl.EnableVertexAttribArray(3 + a);
        gl.VertexAttribPointer(3 + a, 4, GL_FLOAT, GL_FALSE, sizeof(ParticleInstance),
                               (const void *)(size_t)(a * 4 * sizeof(float)));
        gl.VertexAttribDivisor(3 + a, 1);
    }
    gl.BindVertexArray(n->vao);
}

void gl_release_particle_objects(GLRenderDevice::Native *n)
{
    if (n->particleSim) {
        GLuint vaos[2] = { n->particleSimVao, n->particleDrawVao };
        gl.DeleteVertexArrays(2, vaos);
        gl.DeleteBuffers(1, &n->particleInstances);
        gl.DeleteProgram(n->particleSim);
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
    if (buffers[0] && gl_usable()) {
        gl.DeleteBuffers(2, buffers);
        const GLuint textures[2] = { slots, shapes };
        gl.DeleteTextures(2, textures);
    }
}

ParticleBuffer *GLRenderDevice::CreateParticleBuffer(uint32_t count, const uint32_t *shapes)
{
    Native *n = native_;
    if (count == 0)
        return NULL;
    GLParticleBuffer *pb = new GLParticleBuffer();
    pb->count = count;
    pb->rows = (count + PARTICLE_ROW_SLOTS - 1) / PARTICLE_ROW_SLOTS;
    if (!n->active || !n->particleSim)
        return pb;

    const uint32_t all = pb->rows * PARTICLE_ROW_SLOTS;
    std::vector<float> slots((size_t)all * 12, 0.0f);
    for (uint32_t i = 0; i < all; i++) {
        slots[i * 12 + 3] = -1.0f;  // dead
        slots[i * 12 + 7] = shapes && i < count ? (float)shapes[i] : 0.0f;
    }
    gl.GenBuffers(2, pb->buffers);
    for (GLuint b : pb->buffers) {
        gl.BindBuffer(GL_ARRAY_BUFFER, b);
        gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(slots.size() * sizeof(float)),
                      slots.data(), GL_DYNAMIC_COPY);
    }
    GLuint textures[2];
    gl.GenTextures(2, textures);
    pb->slots = textures[0];
    pb->shapes = textures[1];
    bind_float_texture(GL_TEXTURE1, pb->slots);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, PARTICLE_ROW_SLOTS * 3, (GLsizei)pb->rows, 0,
                  GL_RGBA, GL_FLOAT, slots.data());
    gl.ActiveTexture(GL_TEXTURE0);
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

void GLRenderDevice::StepParticles(ParticleBuffer *buf, const ParticleStep &s)
{
    GLParticleBuffer *pb = static_cast<GLParticleBuffer *>(buf);
    Native *n = native_;
    if (!pb || !pb->buffers[0] || !n->active)
        return;
    const GLuint p = n->particleSim;
    auto loc = [p](const char *name) { return gl.GetUniformLocation(p, name); };
    auto vec3 = [&](const char *name, const Vec3 &v) {
        const float f[3] = { v.x, v.y, v.z };
        gl.Uniform3fv(loc(name), 1, f);
    };

    const ParticleEmit &e = s.emit;
    const GLint emit[4] = { (GLint)std::min(e.count, pb->count),
                            (GLint)(e.firstSlot % pb->count), (GLint)pb->count,
                            (GLint)e.seed };
    gl.UseProgram(p);
    gl.Uniform4iv(loc("uEmit"), 1, emit);
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

    const ParticleField &f = s.field;
    gl.Uniform1f(loc("uDt"), s.dt);
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
    gl.Uniform1f(loc("uFadeStep"), f.fadeStep);

    const int src = pb->current, dst = 1 - pb->current;
    gl.BindVertexArray(n->particleSimVao);
    gl.BindBuffer(GL_ARRAY_BUFFER, pb->buffers[src]);
    for (GLuint a = 0; a < 3; a++) {
        gl.EnableVertexAttribArray(a);
        gl.VertexAttribPointer(a, 4, GL_FLOAT, GL_FALSE, kSlotStride,
                               (const void *)(size_t)(a * 4 * sizeof(float)));
    }
    gl.BindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, pb->buffers[dst]);
    gl.Enable(GL_RASTERIZER_DISCARD);
    gl.BeginTransformFeedback(GL_POINTS);
    gl.DrawArrays(GL_POINTS, 0, (GLsizei)(pb->rows * PARTICLE_ROW_SLOTS));
    gl.EndTransformFeedback();
    gl.Disable(GL_RASTERIZER_DISCARD);
    gl.BindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0);
    pb->current = dst;

    // The draw reads the slots as a texture: copy them in, on the device.
    gl.BindBuffer(GL_PIXEL_UNPACK_BUFFER, pb->buffers[dst]);
    bind_float_texture(GL_TEXTURE1, pb->slots);
    gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, PARTICLE_ROW_SLOTS * 3, (GLsizei)pb->rows, GL_RGBA,
                     GL_FLOAT, (const void *)0);
    gl.BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    gl.ActiveTexture(GL_TEXTURE0);

    gl.BindVertexArray(n->vao);
    gl.UseProgram(n->program);
}

bool GLRenderDevice::DrawParticles(const ParticleBuffer *buf, const ParticleLook &look,
                                   const ParticleInstance *instances, uint32_t count)
{
    const GLParticleBuffer *pb = static_cast<const GLParticleBuffer *>(buf);
    Native *n = native_;
    if (!n->active || gl_fx_nodraw())
        return true;
    if (!pb || !pb->buffers[0] || count == 0 || (look.tumbling && pb->shapeCount == 0))
        return false;

    gl_prepare_draw(n, state_, (float)((double)mode_->dwWidth / (double)mode_->dwHeight),
                    VertexFormat::Lit, 0);
    // Two-sided while tumbling: a shape turned past edge-on still shows.
    if (look.tumbling && state_.raster.cull != CullMode::None) {
        gl.Disable(GL_CULL_FACE);
        n->dirty |= GLDirty::Raster;
    }

    bind_float_texture(GL_TEXTURE1, pb->slots);
    bind_float_texture(GL_TEXTURE2, pb->shapes);
    gl.ActiveTexture(GL_TEXTURE0);

    const GLuint p = n->particleDraw;
    auto loc = [p](const char *name) { return gl.GetUniformLocation(p, name); };
    gl.UseProgram(p);
    gl.Uniform1i(loc("uShapeCount"), (GLint)pb->shapeCount);
    gl.Uniform1i(loc("uRowSlots"), (GLint)PARTICLE_ROW_SLOTS);
    gl.Uniform1i(loc("uTumbling"), look.tumbling ? 1 : 0);
    const float facing[3] = { look.facing.x, look.facing.y, look.facing.z };
    gl.Uniform3fv(loc("uFacing"), 1, facing);
    gl.Uniform1f(loc("uSize"), look.size);
    gl.Uniform1f(loc("uTime"), look.time);
    gl.Uniform2fv(loc("uCornerUV"), 6, &look.uv[0][0]);

    gl.BindVertexArray(n->particleDrawVao);
    gl.BindBuffer(GL_ARRAY_BUFFER, n->particleInstances);
    gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(count * sizeof(ParticleInstance)), instances,
                  GL_STREAM_DRAW);
    gl.DrawArraysInstanced(GL_TRIANGLES, 0, (GLsizei)(pb->count * 6), (GLsizei)count);

    gl.BindVertexArray(n->vao);
    gl.UseProgram(n->program);
    return true;
}
