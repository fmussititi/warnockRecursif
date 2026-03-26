#include "warnock.h"
#include <math.h>
#include <stdbool.h>

void subdivise(Region* r, Region regions[4]) {
    regions[0] = (Region){r->x1,              (r->y1+r->y2)/2, (r->x1+r->x2)/2, r->y2};
    regions[1] = (Region){(r->x1+r->x2)/2,   (r->y1+r->y2)/2, r->x2,           r->y2};
    regions[2] = (Region){r->x1,              r->y1,           (r->x1+r->x2)/2, (r->y1+r->y2)/2};
    regions[3] = (Region){(r->x1+r->x2)/2,   r->y1,           r->x2,           (r->y1+r->y2)/2};
}

static inline bool PointInRegion(Vector2 p, Region* r) {
    return (p.x >= r->x1 && p.x <= r->x2 &&
            p.y >= r->y1 && p.y <= r->y2);
}

static bool SegmentsIntersect(Vector2 p1, Vector2 q1, Vector2 p2, Vector2 q2) {
    #define ORIENT(a,b,c) ((b.x-a.x)*(c.y-a.y) - (b.y-a.y)*(c.x-a.x))
    float o1 = ORIENT(p1,q1,p2);
    float o2 = ORIENT(p1,q1,q2);
    float o3 = ORIENT(p2,q2,p1);
    float o4 = ORIENT(p2,q2,q1);
    #undef ORIENT
    return (o1*o2 < 0 && o3*o4 < 0);
}

bool TriangleIntersectsRegion(Region* r, Poly* tri) {
    Vector2 corners[4] = {
        {r->x1, r->y1}, {r->x2, r->y1},
        {r->x2, r->y2}, {r->x1, r->y2}
    };
    for (int i = 0; i < 4; i++)
        if (CheckCollisionPointTriangle(corners[i], tri->p0, tri->p1, tri->p2))
            return true;

    if (PointInRegion(tri->p0, r)) return true;
    if (PointInRegion(tri->p1, r)) return true;
    if (PointInRegion(tri->p2, r)) return true;

    Vector2 rectEdges[4][2] = {
        {{r->x1,r->y1},{r->x2,r->y1}}, {{r->x2,r->y1},{r->x2,r->y2}},
        {{r->x2,r->y2},{r->x1,r->y2}}, {{r->x1,r->y2},{r->x1,r->y1}}
    };
    Vector2 triEdges[3][2] = {
        {tri->p0,tri->p1}, {tri->p1,tri->p2}, {tri->p2,tri->p0}
    };
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 4; j++)
            if (SegmentsIntersect(triEdges[i][0], triEdges[i][1],
                                  rectEdges[j][0], rectEdges[j][1]))
                return true;

    return false;
}

bool AABBOverlap(Region* r, Poly* p) {
    return !(p->maxX < r->x1 || p->minX > r->x2 ||
             p->maxY < r->y1 || p->minY > r->y2);
}

int region_fully_covered(Region* r, Poly* tri) {
    Vector2 c[4] = {
        {r->x1,r->y1}, {r->x2,r->y1},
        {r->x1,r->y2}, {r->x2,r->y2}
    };
    for (int i = 0; i < 4; i++)
        if (!CheckCollisionPointTriangle(c[i], tri->p0, tri->p1, tri->p2))
            return 0;
    return 1;
}

int region_outside(Region* r, Poly* tri) {
    int minx = tri->p0.x < tri->p1.x ? (tri->p0.x < tri->p2.x ? (int)tri->p0.x : (int)tri->p2.x)
                                      : (tri->p1.x < tri->p2.x ? (int)tri->p1.x : (int)tri->p2.x);
    int miny = tri->p0.y < tri->p1.y ? (tri->p0.y < tri->p2.y ? (int)tri->p0.y : (int)tri->p2.y)
                                      : (tri->p1.y < tri->p2.y ? (int)tri->p1.y : (int)tri->p2.y);
    int maxx = tri->p0.x > tri->p1.x ? (tri->p0.x > tri->p2.x ? (int)tri->p0.x : (int)tri->p2.x)
                                      : (tri->p1.x > tri->p2.x ? (int)tri->p1.x : (int)tri->p2.x);
    int maxy = tri->p0.y > tri->p1.y ? (tri->p0.y > tri->p2.y ? (int)tri->p0.y : (int)tri->p2.y)
                                      : (tri->p1.y > tri->p2.y ? (int)tri->p1.y : (int)tri->p2.y);
    return (maxx < r->x1 || minx > r->x2 || maxy < r->y1 || miny > r->y2);
}

void drawRegionZBuffer(RenderContext* ctx, Region* r, Poly* polys, int* indices, int count)
{
    int width  = r->x2 - r->x1;
    int height = r->y2 - r->y1;

    float zbuf[ctx->tile_size * ctx->tile_size];
    for (int i = 0; i < width * height; i++)
        zbuf[i] = 1e9f;

    for (int i = 0; i < count; i++) {
        Poly* tri = &polys[indices[i]];

        int minX = (int)fmaxf(r->x1, floorf(tri->minX));
        int maxX = (int)fminf(r->x2-1, ceilf(tri->maxX));
        int minY = (int)fmaxf(r->y1, floorf(tri->minY));
        int maxY = (int)fminf(r->y2-1, ceilf(tri->maxY));

        if (minX > maxX || minY > maxY) continue;

        EdgeEq e0 = makeEdge(tri->p0, tri->p1);
        EdgeEq e1 = makeEdge(tri->p1, tri->p2);
        EdgeEq e2 = makeEdge(tri->p2, tri->p0);

        float area    = (tri->p1.x-tri->p0.x)*(tri->p2.y-tri->p0.y) -
                        (tri->p2.x-tri->p0.x)*(tri->p1.y-tri->p0.y);
        float invArea = 1.0f / area;

        float w0_row = evalEdge(e0, minX+0.5f, minY+0.5f);
        float w1_row = evalEdge(e1, minX+0.5f, minY+0.5f);
        float w2_row = evalEdge(e2, minX+0.5f, minY+0.5f);

        for (int y = minY; y <= maxY; y += height) {
            float w0 = w0_row, w1 = w1_row, w2 = w2_row;
            for (int x = minX; x <= maxX; x += width) {
                if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
                    float alpha = w1 * invArea;
                    float beta  = w2 * invArea;
                    float gamma = 1.0f - alpha - beta;
                    float z     = alpha*tri->z0 + beta*tri->z1 + gamma*tri->z2;

                    int lx    = x - r->x1;
                    int ly    = y - r->y1;
                    int index = ly * width + lx;

                    if (z < zbuf[index]) {
                        zbuf[index] = z;
                        DrawRectangle(x, ctx->screenHeight - y, width+1, height+1, tri->couleur);
                    }
                }
                w0 += e0.A; w1 += e1.A; w2 += e2.A;
            }
            w0_row += e0.B; w1_row += e1.B; w2_row += e2.B;
        }
    }
}

static int isBehindPlane(Poly* A, Poly* B)
{
    Vector3 A0 = {A->p0.x, A->p0.y, A->z0};
    Vector3 A1 = {A->p1.x, A->p1.y, A->z1};
    Vector3 A2 = {A->p2.x, A->p2.y, A->z2};
    Vector3 u  = Vector3Subtract(A1, A0);
    Vector3 v  = Vector3Subtract(A2, A0);
    Vector3 n  = Vector3CrossProduct(u, v);

    Vector3 Bp[3] = {
        {B->p0.x, B->p0.y, B->z0},
        {B->p1.x, B->p1.y, B->z1},
        {B->p2.x, B->p2.y, B->z2}
    };
    for (int i = 0; i < 3; i++) {
        Vector3 w = Vector3Subtract(Bp[i], A0);
        if (Vector3DotProduct(n, w) > 0) return 0;
    }
    return 1;
}

static int hides(Poly* A, Poly* B) {
    return isBehindPlane(A, B);
}

static int isFrontMost(Poly* A, Poly* polys, int* indices, int count)
{
    for (int i = 0; i < count; i++) {
        Poly* B = &polys[indices[i]];
        if (B == A) continue;
        if (!hides(A, B)) return 0;
    }
    return 1;
}

void warnock(RenderContext* ctx, Region* r, int* indices, int count, int depth)
{
    if (!r) return;

    int left   = r->x1;
    int top    = ctx->screenHeight - r->y2;
    int width  = r->x2 - r->x1;
    int height = r->y2 - r->y1;

    if (depth >= ctx->tree_depth) {
        drawRegionZBuffer(ctx, r, ctx->polys, indices, count);
        return;
    }

    int localIndices[ctx->max_poly];
    int localCount = 0;

    for (int i = 0; i < count; i++) {
        int idx = indices[i];
        if (localCount >= ctx->max_poly) break;
        if (!ctx->polys[idx].visible) continue;

        bool overlaps = (width > 20 && height > 20)
            ? AABBOverlap(r, &ctx->polys[idx])
            : TriangleIntersectsRegion(r, &ctx->polys[idx]);

        if (overlaps) localIndices[localCount++] = idx;
    }

    if (localCount == 0) {
        if (ctx->tree_depth) DrawRectangleLines(left, top, width, height, RED);
        return;
    }

    if (localCount == 1) {
        DrawRectangle(left, top, width, height, ctx->polys[localIndices[0]].couleur);
        return;
    }

    for (int i = 0; i < localCount; i++) {
        Poly* A = &ctx->polys[localIndices[i]];
        if (region_fully_covered(r, A) && isFrontMost(A, ctx->polys, localIndices, localCount)) {
            DrawRectangle(left, top, width, height, A->couleur);
            return;
        }
    }

    Region regions[4];
    subdivise(r, regions);
    for (int i = 0; i < 4; i++)
        warnock(ctx, &regions[i], localIndices, localCount, depth + 1);
}
