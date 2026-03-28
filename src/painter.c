#include "painter.h"
#include "utils.h"
#include "globals.h"
#include "shading.h"
#include <math.h>
#include <stdlib.h>

// La fonction de rendu
void drawPainter(RenderContext* ctx)
{
    // 1. Trier du plus loin au plus proche
    qsort(ctx->polys, ctx->polyCount, sizeof(Poly), compare_zmax_desc);

    // 2. Dessiner dans l'ordre — le dernier dessiné est devant
    for (int i = 0; i < ctx->polyCount; i++) {
        Poly* tri = &ctx->polys[i];
        if (!tri->visible) continue;
     
        if (ctx->gouraudShading) {       
            int minX = (int)fmaxf(0,                  floorf(tri->minX));
            int maxX = (int)fminf(ctx->screenWidth-1,  ceilf(tri->maxX));
            int minY = (int)fmaxf(0,                  floorf(tri->minY));
            int maxY = (int)fminf(ctx->screenHeight-1, ceilf(tri->maxY));

            float area    = (tri->p1.x-tri->p0.x)*(tri->p2.y-tri->p0.y) -
                            (tri->p2.x-tri->p0.x)*(tri->p1.y-tri->p0.y);
            float invArea = 1.0f / area;

            for (int y = minY; y <= maxY; y++) {
                for (int x = minX; x <= maxX; x++) {
                    Vector2 p = { x + 0.5f, y + 0.5f };

                    if (!CheckCollisionPointTriangle(p, tri->p0, tri->p1, tri->p2))
                        continue;

                    // Coordonnées barycentriques
                    float w0 = ((tri->p1.x-p.x)*(tri->p2.y-p.y) -
                                (tri->p2.x-p.x)*(tri->p1.y-p.y)) * invArea;
                    float w1 = ((tri->p2.x-p.x)*(tri->p0.y-p.y) -
                                (tri->p0.x-p.x)*(tri->p2.y-p.y)) * invArea;
                    float w2 = 1.0f - w0 - w1;

                    // Interpolation couleur Gouraud
                    Color final = {
                        (unsigned char)(w0*tri->c0.r + w1*tri->c1.r + w2*tri->c2.r),
                        (unsigned char)(w0*tri->c0.g + w1*tri->c1.g + w2*tri->c2.g),
                        (unsigned char)(w0*tri->c0.b + w1*tri->c1.b + w2*tri->c2.b),
                        255
                    };

                    // Écriture directe dans le framebuffer — pas de test Z
                    int fbY = ctx->screenHeight - y;
                    if (fbY >= 0 && fbY < ctx->screenHeight)
                        framebuffer[fbY * ctx->screenWidth + x] = final;                    

                    //DrawPixel(x, ctx->screenHeight - y, final);
                }
            }
        }
        else
            // Couleur uniforme
            DrawTriangleFramebuffer(ctx, tri, tri->couleur);
    }
}