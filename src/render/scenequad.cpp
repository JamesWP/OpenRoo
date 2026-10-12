/* Scene_RenderSceneObjects's strided draw for kind-2 (quad) theme
 * objects, called from sceneobjects.cpp once per sub-object per visible tile.
 *
 * The quads are stored as SceneQuadVertex (two UV pairs) but only the second
 * pair is ever sampled, so they are drawn as VertexFormat::Diffuse1 with that
 * pair as set 0.  (Drawing them as Diffuse2 with set 1 unfilled was a wild
 * read -- CRASH.md.) */

#include <strings.h>
#include <string.h>
#include <atomic>
#include <stdint.h>
#include "scenequad.h"
#include "sysdev.h"
#include "logger.h"

#define QUAD_LOG_FIRST  8

bool SceneQuad_Draw(RenderDevice *dev, const SceneQuadVertex *q)
{
    static std::atomic<long> logged = 0;
    if (++logged <= QUAD_LOG_FIRST)
        g_logger.write("scenequad: dev=%p q=%p\n", (void *)dev, (const void *)q);

    Diffuse1Vertex v[4];
    for (int i = 0; i < 4; i++) {
        v[i] = { q[i].x, q[i].y, q[i].z, q[i].diffuse, q[i].u1, q[i].v1 };
    }
    return dev->Draw(Prim::TriangleStrip, VertexFormat::Diffuse1, v, 4);
}
