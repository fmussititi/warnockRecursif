#include "zbuffer.h"
#include "globals.h"
#include <math.h>

void drawTriangleZ(RenderContext* ctx, Poly* tri, float* zbuffer)
{
    int minX = (int)fmaxf(0,                  floorf(fminf(tri->p0.x, fminf(tri->p1.x, tri->p2.x))));
    int maxX = (int)fminf(ctx->screenWidth-1,  ceilf(fmaxf(tri->p0.x, fmaxf(tri->p1.x, tri->p2.x))));
    int minY = (int)fmaxf(0,                  floorf(fminf(tri->p0.y, fminf(tri->p1.y, tri->p2.y))));
    int maxY = (int)fminf(ctx->screenHeight-1, ceilf(fmaxf(tri->p0.y, fmaxf(tri->p1.y, tri->p2.y))));

    for (int y = minY; y <= maxY; y++) {
        for (int x = minX; x <= maxX; x++) {
            Vector2 p = {x + 0.5f, y + 0.5f};

            if (!CheckCollisionPointTriangle(p, tri->p0, tri->p1, tri->p2))
                continue;

            float area = (tri->p1.x-tri->p0.x)*(tri->p2.y-tri->p0.y) -
                         (tri->p2.x-tri->p0.x)*(tri->p1.y-tri->p0.y);

            float w0 = ((tri->p1.x-p.x)*(tri->p2.y-p.y) - (tri->p2.x-p.x)*(tri->p1.y-p.y)) / area;
            float w1 = ((tri->p2.x-p.x)*(tri->p0.y-p.y) - (tri->p0.x-p.x)*(tri->p2.y-p.y)) / area;
            float w2 = ((tri->p0.x-p.x)*(tri->p1.y-p.y) - (tri->p1.x-p.x)*(tri->p0.y-p.y)) / area;

            float z = w0*tri->z0 + w1*tri->z1 + w2*tri->z2;

            int index = y * ctx->screenWidth + x;
            if (z < zbuffer[index]) {
                zbuffer[index] = z;
                Color final = {
                    (unsigned char)(w0*tri->c0.r + w1*tri->c1.r + w2*tri->c2.r),
                    (unsigned char)(w0*tri->c0.g + w1*tri->c1.g + w2*tri->c2.g),
                    (unsigned char)(w0*tri->c0.b + w1*tri->c1.b + w2*tri->c2.b),
                    255
                };

                int fbY = ctx->screenHeight - y;
                if (fbY >= 0 && fbY < ctx->screenHeight)
                    framebuffer[fbY * ctx->screenWidth + x] = final;
                //DrawPixel(x, ctx->screenHeight - y, final);
            }
        }
    }
}
