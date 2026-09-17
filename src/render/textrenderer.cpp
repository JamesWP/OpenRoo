/* TextRenderer -- placeholder; see textrenderer.h.
 *
 * All four entry points are called through, at their original addresses,
 * until this file takes them over.
 */
#include "textrenderer.h"

#define THISCALL __attribute__((thiscall))

typedef void (THISCALL *text_fn)(TextRenderer *self, float x, float y,
                                 float cellW, float cellH, float scale,
                                 const char *str, Direct3D *d3d, DWORD zero,
                                 void *fontA, void *fontB);
typedef void (THISCALL *bigtext_fn)(TextRenderer *self, float x, float y,
                                    float cellW, float cellH, float scale,
                                    const char *str, Direct3D *d3d, DWORD zero,
                                    DWORD colourA, DWORD colourB,
                                    float outline, float wobble, int n);

#define ORIG_RENDER_TEXT     ((text_fn)0x00413690)
#define ORIG_RENDER_TEXT_R   ((text_fn)0x00413e30)
#define ORIG_DRAW_CENTERED   ((text_fn)0x00413d00)
#define ORIG_DRAW_BIG_TEXT   ((bigtext_fn)0x00413d90)

void TextRenderer::drawLeft(float x, float y, float cellW, float cellH,
                            float scale, const char *str, Direct3D *d3d,
                            DWORD zero, void *fontA, void *fontB)
{
    ORIG_RENDER_TEXT(this, x, y, cellW, cellH, scale, str, d3d, zero,
                     fontA, fontB);
}

void TextRenderer::drawRight(float x, float y, float cellW, float cellH,
                             float scale, const char *str, Direct3D *d3d,
                             DWORD zero, void *fontA, void *fontB)
{
    ORIG_RENDER_TEXT_R(this, x, y, cellW, cellH, scale, str, d3d, zero,
                       fontA, fontB);
}

void TextRenderer::drawCentered(float x, float y, float cellW, float cellH,
                                float scale, const char *str, Direct3D *d3d,
                                DWORD zero, void *fontA, void *fontB)
{
    ORIG_DRAW_CENTERED(this, x, y, cellW, cellH, scale, str, d3d, zero,
                       fontA, fontB);
}

void TextRenderer::drawBig(float x, float y, float cellW, float cellH,
                           float scale, const char *str, Direct3D *d3d,
                           DWORD zero, DWORD colourA, DWORD colourB,
                           float outline, float wobble, int n)
{
    ORIG_DRAW_BIG_TEXT(this, x, y, cellW, cellH, scale, str, d3d, zero,
                       colourA, colourB, outline, wobble, n);
}
