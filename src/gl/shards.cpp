/* RenderDevice's shattered meshes (shardtypes.h): the buffer, a texture of
 * the mesh's triangles, and the draw, whose vertex shader works out each
 * shard where the burst has it.  The program is in shaders.cpp.
 *
 * Texture unit 1 carries the buffer's texture during the draw, as it does a
 * particle buffer's slots; unit 0, the game's, is left as it was and active.
 * The draw has a vertex array of its own, with nothing enabled. */

#include "glnative_internal.h"
#include "logger.h"
#include <vector>

void gl_create_shard_objects(GLRenderDevice::Native *n)
{
    char error[900] = "";
    n->shardDraw = gl_build_shard_program(error, sizeof(error));
    gl.UseProgram(n->program);
    if (!n->shardDraw) {
        g_logger.write("renderdevice: no shattered meshes: %s\n", error);
        return;
    }
    gl.GenVertexArrays(1, &n->shardVao);
}

void gl_release_shard_objects(GLRenderDevice::Native *n)
{
    if (n->shardDraw) {
        gl.DeleteVertexArrays(1, &n->shardVao);
        gl.DeleteProgram(n->shardDraw);
    }
}

GLShardBuffer::~GLShardBuffer()
{
    if (texture && gl_usable())
        gl.DeleteTextures(1, &texture);
}

ShardBuffer *GLRenderDevice::CreateShardBuffer(VertexFormat format, const void *verts,
                                               uint32_t count)
{
    Native *n = native_;
    if (format != VertexFormat::Normal2 || count == 0 || count % 3 != 0 || !verts)
        return NULL;
    GLShardBuffer *sb = new GLShardBuffer();
    sb->count = count;
    if (!n->active || !n->shardDraw)
        return sb;

    // Normal2: position, normal, then two UV sets, of which the first is drawn.
    struct Normal2Vertex { float pos[3], normal[3], uv0[2], uv1[2]; };
    const Normal2Vertex *src = static_cast<const Normal2Vertex *>(verts);
    const uint32_t rows = (count + SHARD_ROW_VERTICES - 1) / SHARD_ROW_VERTICES;
    std::vector<float> texels((size_t)rows * SHARD_ROW_VERTICES * 8, 0.0f);
    for (uint32_t i = 0; i < count; i++) {
        float *t = &texels[(size_t)i * 8];
        const Normal2Vertex &v = src[i];
        t[0] = v.pos[0];    t[1] = v.pos[1];    t[2] = v.pos[2];    t[3] = v.uv0[0];
        t[4] = v.normal[0]; t[5] = v.normal[1]; t[6] = v.normal[2]; t[7] = v.uv0[1];
    }
    gl.GenTextures(1, &sb->texture);
    gl.ActiveTexture(GL_TEXTURE1);
    gl.BindTexture(GL_TEXTURE_2D, sb->texture);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, SHARD_ROW_VERTICES * 2, (GLsizei)rows, 0,
                  GL_RGBA, GL_FLOAT, texels.data());
    gl.ActiveTexture(GL_TEXTURE0);
    return sb;
}

bool GLRenderDevice::DrawShards(const ShardBuffer *buf, const ShardBurst &b)
{
    const GLShardBuffer *sb = static_cast<const GLShardBuffer *>(buf);
    Native *n = native_;
    if (!n->active || gl_fx_nodraw())
        return true;
    if (!sb || !sb->texture)
        return false;

    gl_prepare_draw(n, state_, (float)((double)mode_->dwWidth / (double)mode_->dwHeight),
                    VertexFormat::Normal2, DrawFlag::NoLight);
    // Two-sided: a shard turned past edge-on still shows.
    if (state_.raster.cull != CullMode::None) {
        gl.Disable(GL_CULL_FACE);
        n->dirty |= GLDirty::Raster;
    }

    gl.ActiveTexture(GL_TEXTURE1);
    gl.BindTexture(GL_TEXTURE_2D, sb->texture);
    gl.ActiveTexture(GL_TEXTURE0);

    const GLuint p = n->shardDraw;
    auto loc = [p](const char *name) { return gl.GetUniformLocation(p, name); };
    gl.UseProgram(p);
    gl.Uniform1i(loc("uRowVertices"), (GLint)SHARD_ROW_VERTICES);
    const float origin[3] = { b.origin.x, b.origin.y, b.origin.z };
    gl.Uniform3fv(loc("uOrigin"), 1, origin);
    const float speed[2] = { b.speedMean, b.speedVariance };
    gl.Uniform2fv(loc("uSpeed"), 1, speed);
    gl.Uniform1f(loc("uSpin"), b.spin);
    const float lasts = b.dropRate > 0.0f ? (float)(sb->count / 3) / b.dropRate : 0.0f;
    const float drop[2] = { lasts, b.shrink };
    gl.Uniform2fv(loc("uDrop"), 1, drop);
    gl.Uniform1i(loc("uSeed"), (GLint)b.seed);
    gl.Uniform1f(loc("uTime"), b.time);

    gl.BindVertexArray(n->shardVao);
    gl.DrawArrays(GL_TRIANGLES, 0, (GLsizei)sb->count);

    gl.BindVertexArray(n->vao);
    gl.UseProgram(n->program);
    return true;
}
