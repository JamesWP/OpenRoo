/* RenderDevice's particles (particletypes.h): the buffers, the step by
 * transform feedback and the instanced draw.  The programs are in
 * shaders.cpp.
 *
 * Texture unit 1 carries the particle textures, the tables for a step and
 * the corners for a draw; unit 0, the game's, is left as it was and active.
 * Each pass has a vertex array of its own, so the main one's attributes and
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
    gl.GenTextures(1, &n->particleShapes);
    n->particleShapeRows = 0;
    gl.BindVertexArray(n->vao);
}

void gl_release_particle_objects(GLRenderDevice::Native *n)
{
    if (n->particleSim) {
        GLuint vaos[2] = { n->particleSimVao, n->particleDrawVao };
        gl.DeleteVertexArrays(2, vaos);
        gl.DeleteTextures(1, &n->particleShapes);
        gl.DeleteProgram(n->particleSim);
        gl.DeleteProgram(n->particleDraw);
    }
}

/* A float texture on unit 1, sampled only by texelFetch. */
static void bind_float_texture(GLuint tex)
{
    gl.ActiveTexture(GL_TEXTURE1);
    gl.BindTexture(GL_TEXTURE_2D, tex);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

/* Points the bound vertex array's attributes 0..2 at a slot buffer. */
static void bind_slots(GLuint buffer, GLuint divisor)
{
    gl.BindBuffer(GL_ARRAY_BUFFER, buffer);
    for (GLuint a = 0; a < 3; a++) {
        gl.EnableVertexAttribArray(a);
        gl.VertexAttribPointer(a, 4, GL_FLOAT, GL_FALSE, kSlotStride,
                               (const void *)(size_t)(a * 4 * sizeof(float)));
        gl.VertexAttribDivisor(a, divisor);
    }
}

GLParticleBuffer::~GLParticleBuffer()
{
    if (buffers[0] && gl_usable()) {
        gl.DeleteBuffers(2, buffers);
        gl.DeleteTextures(1, &tables);
    }
}

ParticleBuffer *GLRenderDevice::CreateParticleBuffer(uint32_t count, const uint32_t *shapes)
{
    Native *n = native_;
    if (count == 0)
        return NULL;
    GLParticleBuffer *pb = new GLParticleBuffer();
    pb->count = count;
    if (!n->active || !n->particleSim)
        return pb;

    std::vector<float> slots((size_t)count * 12, 0.0f);
    for (uint32_t i = 0; i < count; i++) {
        slots[i * 12 + 3] = -1.0f;                         // dead
        slots[i * 12 + 7] = shapes ? (float)shapes[i] : 0.0f;
    }
    gl.GenBuffers(2, pb->buffers);
    for (GLuint b : pb->buffers) {
        gl.BindBuffer(GL_ARRAY_BUFFER, b);
        gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(slots.size() * sizeof(float)),
                      slots.data(), GL_DYNAMIC_COPY);
    }
    gl.GenTextures(1, &pb->tables);
    return pb;
}

void GLRenderDevice::SetParticleTables(ParticleBuffer *buf, const ParticleTables &t)
{
    GLParticleBuffer *pb = static_cast<GLParticleBuffer *>(buf);
    if (!pb || !pb->tables || !gl_usable())
        return;
    const uint32_t counts[4] = { t.posCount, t.velCount, t.lifeCount, t.colourCount };
    const uint32_t width = std::max({ counts[0], counts[1], counts[2], counts[3], 1u });
    std::vector<float> texels((size_t)width * 4 * 4, 0.0f);
    for (uint32_t i = 0; i < t.posCount; i++)
        memcpy(&texels[(0 * width + i) * 4], &t.pos[i * 3], 3 * sizeof(float));
    for (uint32_t i = 0; i < t.velCount; i++)
        memcpy(&texels[(1 * width + i) * 4], &t.vel[i * 3], 3 * sizeof(float));
    for (uint32_t i = 0; i < t.lifeCount; i++)
        texels[(2 * width + i) * 4] = t.life[i];
    for (uint32_t i = 0; i < t.colourCount; i++) {
        float *c = &texels[(3 * width + i) * 4];
        const uint32_t argb = t.colour[i];
        c[0] = (float)((argb >> 16) & 255);
        c[1] = (float)((argb >> 8) & 255);
        c[2] = (float)(argb & 255);
        c[3] = (float)(argb >> 24);
    }
    for (int i = 0; i < 4; i++)
        pb->tableCounts[i] = (int)counts[i];
    bind_float_texture(pb->tables);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, (GLsizei)width, 4, 0, GL_RGBA, GL_FLOAT,
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

    // Emission needs every table it reads.
    const ParticleEmit &e = s.emit;
    bool tables = true;
    for (int c : pb->tableCounts)
        tables = tables && c > 0;
    const GLint emit[4] = { tables ? (GLint)std::min(e.count, pb->count) : 0,
                            (GLint)(e.firstSlot % pb->count), (GLint)pb->count,
                            (GLint)e.velStride };
    const GLint cursor[4] = { (GLint)e.pos, (GLint)e.vel, (GLint)e.life, (GLint)e.colour };

    gl.UseProgram(p);
    gl.Uniform4iv(loc("uCounts"), 1, pb->tableCounts);
    gl.Uniform4iv(loc("uCursor"), 1, cursor);
    gl.Uniform4iv(loc("uEmit"), 1, emit);
    gl.Uniform1f(loc("uDt"), s.dt);
    gl.Uniform1f(loc("uPosScale"), e.posScale);
    // Row-major with row vectors is column-major with column vectors.
    gl.UniformMatrix4fv(loc("uPosMatrix"), 1, GL_FALSE, e.posMatrix.m);
    vec3("uPosOffset", e.posOffset);
    vec3("uVelOffset", e.velOffset);

    const ParticleField &f = s.field;
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

    bind_float_texture(pb->tables);
    gl.ActiveTexture(GL_TEXTURE0);

    const int src = pb->current, dst = 1 - pb->current;
    gl.BindVertexArray(n->particleSimVao);
    bind_slots(pb->buffers[src], 0);
    gl.BindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, pb->buffers[dst]);
    gl.Enable(GL_RASTERIZER_DISCARD);
    gl.BeginTransformFeedback(GL_POINTS);
    gl.DrawArrays(GL_POINTS, 0, (GLsizei)pb->count);
    gl.EndTransformFeedback();
    gl.Disable(GL_RASTERIZER_DISCARD);
    gl.BindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0);
    pb->current = dst;

    gl.BindVertexArray(n->vao);
    gl.UseProgram(n->program);
}

bool GLRenderDevice::DrawParticles(const ParticleBuffer *buf, const ParticleLook &look)
{
    const GLParticleBuffer *pb = static_cast<const GLParticleBuffer *>(buf);
    Native *n = native_;
    if (!n->active || gl_fx_nodraw())
        return true;
    if (!pb || !pb->buffers[0] || look.shapeCount == 0 || !look.shapes)
        return false;

    gl_prepare_draw(n, state_, (float)((double)mode_->dwWidth / (double)mode_->dwHeight),
                    VertexFormat::Lit, 0);

    // The corners: one row of six per shape.
    std::vector<float> texels((size_t)look.shapeCount * 6 * 4, 0.0f);
    for (uint32_t s = 0; s < look.shapeCount; s++)
        for (int c = 0; c < 6; c++)
            memcpy(&texels[(s * 6 + c) * 4], look.shapes[s][c], 3 * sizeof(float));
    bind_float_texture(n->particleShapes);
    if (look.shapeCount > n->particleShapeRows) {
        gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 6, (GLsizei)look.shapeCount, 0, GL_RGBA,
                      GL_FLOAT, texels.data());
        n->particleShapeRows = look.shapeCount;
    } else {
        gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 6, (GLsizei)look.shapeCount, GL_RGBA,
                         GL_FLOAT, texels.data());
    }
    gl.ActiveTexture(GL_TEXTURE0);

    const GLuint p = n->particleDraw;
    gl.UseProgram(p);
    gl.Uniform1i(gl.GetUniformLocation(p, "uShapeCount"), (GLint)look.shapeCount);
    gl.Uniform1i(gl.GetUniformLocation(p, "uPerParticle"), look.perParticleShape ? 1 : 0);
    gl.Uniform2fv(gl.GetUniformLocation(p, "uCornerUV"), 6, &look.uv[0][0]);

    gl.BindVertexArray(n->particleDrawVao);
    bind_slots(pb->buffers[pb->current], 1);
    gl.DrawArraysInstanced(GL_TRIANGLES, 0, 6, (GLsizei)pb->count);

    gl.BindVertexArray(n->vao);
    gl.UseProgram(n->program);
    return true;
}
