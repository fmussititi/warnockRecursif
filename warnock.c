#include "raylib.h"
#include "raymath.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "warnock.h"
#include <stdatomic.h>
#include <pthread.h>

#define NUM_THREADS 16

#define SCREEN_WIDTH 1600
#define SCREEN_HEIGHT 900
#define BACKFACECULLING 1

// Switch rendu type Warnock
#define WARNOCK 0
#define CONTOUR_ARBRE 1
#define TREE_DEPTH 8
#define MAX_POLY 10000
#define REFION_TILE_SIZE 32

// Switch rendu type ZBuffer
#define ZBUFFER 0

// Switch rendu type Tiles
#define TILES 1
#define TILE_SIZE 32
#define DEBUG_TILES 0

int tilesX = SCREEN_WIDTH / TILE_SIZE;
int tilesY = SCREEN_HEIGHT / TILE_SIZE;
atomic_int tilesRemaining;
// Buffer global
static Color framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT];

ThreadData threadData[NUM_THREADS];
pthread_t threads[NUM_THREADS];
pthread_barrier_t barrierStart;  // main attend que les threads soient prêts
pthread_barrier_t barrierEnd;    // main attend que les threads aient fini

#define MAX_TRI_PER_TILE 20000
#define MAX_TILES 2000

typedef struct {
    int count;
    int indices[MAX_TRI_PER_TILE];
    float minZ;
    float maxZ;
} Tile;

Tile tiles[MAX_TILES];

typedef struct {
    float A, B, C;
} EdgeEq;

static inline EdgeEq makeEdge(Vector2 a, Vector2 b)
{
    EdgeEq e;
    e.A = a.y - b.y;
    e.B = b.x - a.x;
    e.C = a.x * b.y - a.y * b.x;
    return e;
}

static inline float evalEdge(EdgeEq e, float x, float y)
{
    return e.A * x + e.B * y + e.C;
}

Color couleurAleatoire() {
    Color couleurs[5] = { RED, GREEN, BLUE, YELLOW, ORANGE };
    return couleurs[GetRandomValue(0, 4)];
}


//******************************** Fonctions pour Warnock renderin ***************************************/

void subdivise(Region* r, Region regions[4]) {
    regions[0] = (Region){r->x1, (r->y1 + r->y2)/2, (r->x1 + r->x2)/2, r->y2};
    regions[1] = (Region){(r->x1 + r->x2)/2, (r->y1 + r->y2)/2, r->x2, r->y2};
    regions[2] = (Region){r->x1, r->y1, (r->x1 + r->x2)/2, (r->y1 + r->y2)/2};
    regions[3] = (Region){(r->x1 + r->x2)/2, r->y1, r->x2, (r->y1 + r->y2)/2};
}

// Vérifie si un point est dans un rectangle
static inline bool PointInRegion(Vector2 p, Region* r) {
    return (p.x >= r->x1 && p.x <= r->x2 &&
            p.y >= r->y1 && p.y <= r->y2);
}

// Vérifie si deux segments (p1,q1) et (p2,q2) s'intersectent
bool SegmentsIntersect(Vector2 p1, Vector2 q1, Vector2 p2, Vector2 q2) {
    #define ORIENT(a,b,c) ((b.x - a.x)*(c.y - a.y) - (b.y - a.y)*(c.x - a.x))
    float o1 = ORIENT(p1,q1,p2);
    float o2 = ORIENT(p1,q1,q2);
    float o3 = ORIENT(p2,q2,p1);
    float o4 = ORIENT(p2,q2,q1);
    #undef ORIENT

    return (o1*o2 < 0 && o3*o4 < 0);
}

// Test intersection triangle / région rectangulaire
bool TriangleIntersectsRegion(Region* r, Poly* tri) {
    // 1. Un coin du rectangle est dans le triangle
    Vector2 corners[4] = {
        {r->x1, r->y1},
        {r->x2, r->y1},
        {r->x2, r->y2},
        {r->x1, r->y2}
    };
    for (int i=0; i<4; i++) {
        if (CheckCollisionPointTriangle(corners[i], tri->p0, tri->p1, tri->p2))
            return true;
    }

    // 2. Un sommet du triangle est dans le rectangle
    if (PointInRegion(tri->p0, r)) return true;
    if (PointInRegion(tri->p1, r)) return true;
    if (PointInRegion(tri->p2, r)) return true;

    // 3. Une arête du triangle coupe une arête du rectangle
    Vector2 rectEdges[4][2] = {
        {{r->x1, r->y1}, {r->x2, r->y1}},
        {{r->x2, r->y1}, {r->x2, r->y2}},
        {{r->x2, r->y2}, {r->x1, r->y2}},
        {{r->x1, r->y2}, {r->x1, r->y1}}
    };
    Vector2 triEdges[3][2] = {
        {tri->p0, tri->p1},
        {tri->p1, tri->p2},
        {tri->p2, tri->p0}
    };
    for (int i=0; i<3; i++) {
        for (int j=0; j<4; j++) {
            if (SegmentsIntersect(triEdges[i][0], triEdges[i][1],
                                  rectEdges[j][0], rectEdges[j][1]))
                return true;
        }
    }

    // Aucun recouvrement
    return false;
}

// Détermine si une région est entièrement recouverte par un polygone
int region_fully_covered(Region* r, Poly* tri) {
    Vector2 c[4] = {
        {r->x1, r->y1}, // coin bas-gauche
        {r->x2, r->y1}, // bas-droit
        {r->x1, r->y2}, // haut-gauche
        {r->x2, r->y2}  // haut-droit
    };

    for (int i=0; i<4; i++) {
        if (!CheckCollisionPointTriangle(c[i], tri->p0, tri->p1, tri->p2)){
            return 0; // un coin n’est pas dedans → pas couvert totalement
        }
    }
    return 1; // tous les coins sont dans le triangle
}

// Détermine si une région est complètement en dehors du polygone
int region_outside(Region* r, Poly* tri) {
    int minx = tri->p0.x < tri->p1.x ? (tri->p0.x < tri->p2.x ? tri->p0.x : tri->p2.x)
                                     : (tri->p1.x < tri->p2.x ? tri->p1.x : tri->p2.x);
    int miny = tri->p0.y < tri->p1.y ? (tri->p0.y < tri->p2.y ? tri->p0.y : tri->p2.y)
                                     : (tri->p1.y < tri->p2.y ? tri->p1.y : tri->p2.y);
    int maxx = tri->p0.x > tri->p1.x ? (tri->p0.x > tri->p2.x ? tri->p0.x : tri->p2.x)
                                     : (tri->p1.x > tri->p2.x ? tri->p1.x : tri->p2.x);
    int maxy = tri->p0.y > tri->p1.y ? (tri->p0.y > tri->p2.y ? tri->p0.y : tri->p2.y)
                                     : (tri->p1.y > tri->p2.y ? tri->p1.y : tri->p2.y);

    return (maxx < r->x1 || minx > r->x2 || maxy < r->y1 || miny > r->y2);
}

static inline bool AABBOverlap(Region* r, Poly* p)
{
    return !(p->maxX < r->x1 || p->minX > r->x2 ||
             p->maxY < r->y1 || p->minY > r->y2);
}

void drawRegionZBuffer(Region* r, Poly* polys, int* indices, int count)
{
    int width  = r->x2 - r->x1;
    int height = r->y2 - r->y1;

    static float zbuf[REFION_TILE_SIZE * REFION_TILE_SIZE];

    for (int i = 0; i < width * height; i++)
        zbuf[i] = 1e9f;

    for (int i = 0; i < count; i++)
    {
        Poly* tri = &polys[indices[i]];

        // bounding box CLAMPÉE à la région
        int minX = (int)fmaxf(r->x1, floorf(tri->minX));
        int maxX = (int)fminf(r->x2-1, ceilf(tri->maxX));
        int minY = (int)fmaxf(r->y1, floorf(tri->minY));
        int maxY = (int)fminf(r->y2-1, ceilf(tri->maxY));

        if (minX > maxX || minY > maxY)
            continue;

        // Edge functions
        EdgeEq e0 = makeEdge(tri->p0, tri->p1);
        EdgeEq e1 = makeEdge(tri->p1, tri->p2);
        EdgeEq e2 = makeEdge(tri->p2, tri->p0);

        float startX = minX + 0.5f;
        float startY = minY + 0.5f;

        float w0_row = evalEdge(e0, startX, startY);
        float w1_row = evalEdge(e1, startX, startY);
        float w2_row = evalEdge(e2, startX, startY);

        float area    = (tri->p1.x - tri->p0.x)*(tri->p2.y - tri->p0.y) -
                        (tri->p2.x - tri->p0.x)*(tri->p1.y - tri->p0.y);
        float invArea = 1.0f / area;

        //for (int y = minY; y <= maxY; y++)
        for (int y = minY; y <= maxY; y+=height)
        {
            float w0 = w0_row;
            float w1 = w1_row;
            float w2 = w2_row;

            //for (int x = minX; x <= maxX; x++)
            for (int x = minX; x <= maxX; x+=width)
            {
                if (w0 >= 0 && w1 >= 0 && w2 >= 0)
                {
                    float alpha = w1 * invArea;
                    float beta  = w2 * invArea;
                    float gamma = 1.0f - alpha - beta;

                    float z = alpha * tri->z0 + beta * tri->z1 + gamma * tri->z2;

                    int lx = x - r->x1;
                    int ly = y - r->y1;
                    int index = ly * width + lx;

                    if (z < zbuf[index])
                    {
                        zbuf[index] = z;
                        DrawRectangle(x, SCREEN_HEIGHT - y, width+1, height+1, tri->couleur);
                    }
                }

                w0 += e0.A;
                w1 += e1.A;
                w2 += e2.A;
            }

            w0_row += e0.B;
            w1_row += e1.B;
            w2_row += e2.B;
        }
    }
}

int noDepthConflict(Poly* list, int count)
{
    for (int i = 0; i < count; i++)
    {
        for (int j = i + 1; j < count; j++)
        {
            Poly* A = &list[i];
            Poly* B = &list[j];

            // si A et B se chevauchent en profondeur → conflit
            if (!(A->zmax < B->zmin || B->zmax < A->zmin))
                return 0;
        }
    }
    return 1;
}

int isBehindPlane(Poly* A, Poly* B)
{
    // construire le plan de A
    Vector3 A0 = {A->p0.x, A->p0.y, A->z0};
    Vector3 A1 = {A->p1.x, A->p1.y, A->z1};
    Vector3 A2 = {A->p2.x, A->p2.y, A->z2};

    Vector3 u = Vector3Subtract(A1, A0);
    Vector3 v = Vector3Subtract(A2, A0);

    // normale du plan
    Vector3 n = Vector3CrossProduct(u, v);

    // équation du plan : n . (P - A0)
    Vector3 Bp[3] = {
        {B->p0.x, B->p0.y, B->z0},
        {B->p1.x, B->p1.y, B->z1},
        {B->p2.x, B->p2.y, B->z2}
    };

    int behind = 1;

    for (int i = 0; i < 3; i++)
    {
        Vector3 w = Vector3Subtract(Bp[i], A0);
        float d = Vector3DotProduct(n, w);

        if (d > 0) // devant le plan
        {
            behind = 0;
            break;
        }
    }

    return behind;
}

int hides(Poly* A, Poly* B)
{
    // test rapide z
    //if (A->zmax < B->zmin)
    //    return 1;

    // test plan
    if (isBehindPlane(A, B))
        return 1;

    return 0;
}

int isFrontMost(Poly* A, Poly* polys, int* indices, int count)
{
    for (int i = 0; i < count; i++)
    {
        Poly* B = &polys[indices[i]];
        if (B == A) continue;

        if (!hides(A, B))
            return 0;
    }
    return 1;
}


// Fonction principale Warnock
void warnock(RenderContext* ctx, Region* r, int* indices, int count, int depth) {
    if (!r) return;

    int left = r->x1;
    int top = SCREEN_HEIGHT - r->y2;
    int width = r->x2 - r->x1;
    int height = r->y2 - r->y1;

    if (depth >= TREE_DEPTH)
    {
        int best = 0;
        float z = ctx->polys[indices[0]].zmin;

        for (int i = 1; i < count; i++)
        {
            if (ctx->polys[indices[i]].zmin < z){
                z = ctx->polys[indices[i]].zmin;
                best = i;
            }
        }
        //DrawRectangle(left, top, width, height, ctx->polys[indices[best]].couleur);
        drawRegionZBuffer(r, ctx->polys, indices, count);
#if CONTOUR_ARBRE == 1
        //DrawRectangleLines(left, top, width, height, ctx->polys[indices[best]].couleur);
#endif
        return;
    }

    int localIndices[MAX_POLY];
    int localCount = 0;

    for (int i = 0; i < count; i++)
    {
        int idx = indices[i];

        if (localCount >= MAX_POLY)
            break;
        
        if (!ctx->polys[idx].visible) continue;

        bool overlaps;
        if (width > 20 && height > 20)
            overlaps = AABBOverlap(r, &ctx->polys[idx]);
        else
            overlaps = TriangleIntersectsRegion(r, &ctx->polys[idx]);

        if (overlaps)
            localIndices[localCount++] = idx;
    }

    // 1. aucun triangle
    if (localCount == 0) {
#if CONTOUR_ARBRE == 1
        DrawRectangleLines(left, top, width, height, RED);
#endif
        return;
    }

    // 2. un seul triangle
    if (localCount == 1) {
        Poly* A = &ctx->polys[localIndices[0]];
        DrawRectangle(left, top, width, height, A->couleur);
        return;
    }

    // 3. Cas trivial : une région entièrement recouverte et devant
    for (int i = 0; i < localCount; i++)
    {
        Poly* A = &ctx->polys[localIndices[i]];
        if (region_fully_covered(r, A) &&
            isFrontMost(A, ctx->polys, localIndices, localCount))
        {
            DrawRectangle(left, top, width, height, A->couleur);
            return;
        }
    }

    Region regions[4];
    subdivise(r, regions);        

    for (int i = 0; i < 4; i++) 
        warnock(ctx, &regions[i], localIndices, localCount, depth + 1);   
}

/***********************************************************************************************************/


/****************************************Fonction pour ZBuffer rendering ***********************************/

//** Dessin d'un triangle dans le zbuffer **/
void drawTriangleZ(Poly* tri, float* zbuffer)
{
    // bounding box écran
    int minX = fmaxf(0, floorf(fminf(tri->p0.x, fminf(tri->p1.x, tri->p2.x))));
    int maxX = fminf(1599, ceilf(fmaxf(tri->p0.x, fmaxf(tri->p1.x, tri->p2.x))));
    int minY = fmaxf(0, floorf(fminf(tri->p0.y, fminf(tri->p1.y, tri->p2.y))));
    int maxY = fminf(899, ceilf(fmaxf(tri->p0.y, fmaxf(tri->p1.y, tri->p2.y))));

    for (int y = minY; y <= maxY; y++)
    {
        for (int x = minX; x <= maxX; x++)
        {
            Vector2 p = {x + 0.5f, y + 0.5f};

            if (CheckCollisionPointTriangle(p, tri->p0, tri->p1, tri->p2))
            {
                // interpolation simple du z (approx)
                //float z = (tri->z0 + tri->z1 + tri->z2) / 3.0f;
                float area = 
                    (tri->p1.x - tri->p0.x)*(tri->p2.y - tri->p0.y) -
                    (tri->p2.x - tri->p0.x)*(tri->p1.y - tri->p0.y);

                float w0 = 
                    (tri->p1.x - p.x)*(tri->p2.y - p.y) -
                    (tri->p2.x - p.x)*(tri->p1.y - p.y);

                float w1 = 
                    (tri->p2.x - p.x)*(tri->p0.y - p.y) -
                    (tri->p0.x - p.x)*(tri->p2.y - p.y);

                float w2 = 
                    (tri->p0.x - p.x)*(tri->p1.y - p.y) -
                    (tri->p1.x - p.x)*(tri->p0.y - p.y);

                w0 /= area;
                w1 /= area;
                w2 /= area;

                float z = w0*tri->z0 + w1*tri->z1 + w2*tri->z2;

                int index = y * 1600 + x;

                if (z < zbuffer[index])
                {
                    zbuffer[index] = z;
                    DrawPixel(x, SCREEN_HEIGHT - y, tri->couleur);
                }
            }
        }
    }
}
/***********************************************************************************************************/

/******************************** Fonctions pour Tiles rendering *******************************************/

void binTriangles(RenderContext* ctx)
{
    for (int t = 0; t < tilesX * tilesY; t++)
        tiles[t].count = 0;

    for (int i = 0; i < ctx->polyCount; i++)
    {
        Poly* p = &ctx->polys[i];

        if (!p->visible) continue;

        // Permet de savoir où ranger le triangle 
        // pixel / TILE_SIZE = tile index
        //int minTileX = (int)(p->minX) / TILE_SIZE;
        //int maxTileX = (int)(p->maxX) / TILE_SIZE;
        //int minTileY = (int)(p->minY) / TILE_SIZE;
        //int maxTileY = (int)(p->maxY) / TILE_SIZE;

        int minTileX = Clamp(p->tileMinX, 0, tilesX-1);
        int maxTileX = Clamp(p->tileMaxX, 0, tilesX-1);
        int minTileY = Clamp(p->tileMinY, 0, tilesY-1);
        int maxTileY = Clamp(p->tileMaxY, 0, tilesY-1);

        for (int ty = minTileY; ty <= maxTileY; ty++)
        {
            for (int tx = minTileX; tx <= maxTileX; tx++)
            {
                int tileIndex = ty * tilesX + tx;
                Tile* tile = &tiles[tileIndex];

                if (tile->count >= MAX_TRI_PER_TILE){
                    printf("OVERFLOW tile [%d,%d] count=%d\n", tx, ty, tile->count);
                    continue;
                }

                // Optimisation --> on range uniquement dans les tiles utiles
                Region myTile = {
                    tx * TILE_SIZE,
                    ty * TILE_SIZE,
                    (tx+1) * TILE_SIZE,
                    (ty+1) * TILE_SIZE
                };

                if (!AABBOverlap(&myTile, p))
                    continue;

                if (!TriangleIntersectsRegion(&myTile, p))
                    continue;

                tile->indices[tile->count++] = i;
            }
        }
    }
}

void drawTile(RenderContext* ctx, int tx, int ty)
{
    int x0 = tx * TILE_SIZE;
    int y0 = ty * TILE_SIZE;

    int x1 = x0 + TILE_SIZE;
    int y1 = y0 + TILE_SIZE;

    Tile* tile = &tiles[ty * tilesX + tx];

    // Z-buffer local tile
    float zbuf[TILE_SIZE * TILE_SIZE];

    for (int i = 0; i < TILE_SIZE * TILE_SIZE; i++)
        zbuf[i] = 1e9f;

    for (int t = 0; t < tile->count; t++)
    {
        Poly* tri = &ctx->polys[tile->indices[t]];

        // bounding box CLAMPÉE au tile
        int minX = (int)fmaxf(x0, floorf(tri->minX));
        int maxX = (int)fminf(x1 - 1, ceilf(tri->maxX));
        int minY = (int)fmaxf(y0, floorf(tri->minY));
        int maxY = (int)fminf(y1 - 1, ceilf(tri->maxY));

        if (minX > maxX || minY > maxY)
            continue;

        // edges
        EdgeEq e0 = makeEdge(tri->p0, tri->p1);
        EdgeEq e1 = makeEdge(tri->p1, tri->p2);
        EdgeEq e2 = makeEdge(tri->p2, tri->p0);

        // point de départ (centre pixel)
        float startX = minX + 0.5f;
        float startY = minY + 0.5f;

        float w0_row = evalEdge(e0, startX, startY);
        float w1_row = evalEdge(e1, startX, startY);
        float w2_row = evalEdge(e2, startX, startY);

        for (int y = minY; y <= maxY; y++)
        {
            float w0 = w0_row;
            float w1 = w1_row;
            float w2 = w2_row;

            for (int x = minX; x <= maxX; x++)
            {
                if (w0 >= 0 && w1 >= 0 && w2 >= 0)
                {
                    float area = (tri->p1.x - tri->p0.x)*(tri->p2.y - tri->p0.y) -
                                 (tri->p2.x - tri->p0.x)*(tri->p1.y - tri->p0.y);

                    float invArea = 1.0f / area;

                    float alpha = w1 * invArea;
                    float beta  = w2 * invArea;
                    float gamma = 1.0f - alpha - beta;

                    float z = alpha * tri->z0 + beta * tri->z1 + gamma * tri->z2;

                    int lx = x - x0;
                    int ly = y - y0;
                    int index = ly * TILE_SIZE + lx;

                    if (z < zbuf[index])
                    {
                        zbuf[index] = z;

                        if (z < tile->minZ) tile->minZ = z;
                        if (z > tile->maxZ) tile->maxZ = z;

                        if (tri->zmin > tile->maxZ)
                            continue;

                        // Interpolation de la normale
                        Vector3 n = {
                            alpha * tri->n0.x + beta * tri->n1.x + gamma * tri->n2.x,
                            alpha * tri->n0.y + beta * tri->n1.y + gamma * tri->n2.y,
                            alpha * tri->n0.z + beta * tri->n1.z + gamma * tri->n2.z,
                        };
                        n = Vector3Normalize(n);
                        n = Vector3Negate(n);

                        // Position 3D interpolée du pixel
                        Vector3 pixelPos = {
                            alpha * tri->v0.x + beta * tri->v1.x + gamma * tri->v2.x,
                            alpha * tri->v0.y + beta * tri->v1.y + gamma * tri->v2.y,
                            alpha * tri->v0.z + beta * tri->v1.z + gamma * tri->v2.z,
                        };

                        // Diffuse
                        float dotNL = Vector3DotProduct(n, ctx->lightDir);

                        // Interpolation UV
                        float u = alpha * tri->uv0.x + beta * tri->uv1.x + gamma * tri->uv2.x;
                        float v = alpha * tri->uv0.y + beta * tri->uv1.y + gamma * tri->uv2.y;

                        // Lire la normal map si disponible
                        if (ctx->normalMap.data != NULL) {
                            int nx = (int)(u * ctx->normalMap.width)  % ctx->normalMap.width;
                            int ny = (int)(v * ctx->normalMap.height) % ctx->normalMap.height;
                            if (nx < 0) nx += ctx->normalMap.width;
                            if (ny < 0) ny += ctx->normalMap.height;

                            Color nc = GetImageColor(ctx->normalMap, nx, ny);

                            // Tangent space normal
                            Vector3 nMap = {
                                (nc.r / 255.0f) * 2.0f - 1.0f,
                                (nc.g / 255.0f) * 2.0f - 1.0f,
                                (nc.b / 255.0f) * 2.0f - 1.0f
                            };

                            // Transformer via TBN vers world space
                            // T et B doivent être transformés avec la rotation aussi
                            Vector3 T = tri->tangent;
                            Vector3 B = tri->bitangent;
                            Vector3 N = n;  // normale interpolée déjà en world space

                            n = Vector3Normalize((Vector3){
                                T.x * nMap.x + B.x * nMap.y + N.x * nMap.z,
                                T.y * nMap.x + B.y * nMap.y + N.y * nMap.z,
                                T.z * nMap.x + B.z * nMap.y + N.z * nMap.z
                            });
                        }

                        // Échantillonnage texture (une seule fois)
                        Color base;
                        if (ctx->texImage.data == NULL) {
                            base = tri->couleur;
                        } else {
                            int texX = (int)(u * ctx->texImage.width)  % ctx->texImage.width;
                            int texY = (int)(v * ctx->texImage.height) % ctx->texImage.height;
                            if (texX < 0) texX += ctx->texImage.width;
                            if (texY < 0) texY += ctx->texImage.height;
                            base = GetImageColor(ctx->texImage, texX, texY);
                        }

                        // Si la face est dos à la lumière → juste ambient
                        if (dotNL <= 0.0f) {
                            framebuffer[(SCREEN_HEIGHT - y) * SCREEN_WIDTH + x] = (Color){
                                (unsigned char)(base.r * 0.2f),
                                (unsigned char)(base.g * 0.2f),
                                (unsigned char)(base.b * 0.2f),
                                255
                            };
                            continue;  // pas de specular sur les faces dos à la lumière
                        }

                        float diffuse = dotNL;

                        // Specular seulement sur les faces éclairées
                        Vector3 viewDir = Vector3Normalize(Vector3Subtract(ctx->cameraPos, pixelPos));
                        Vector3 halfDir = Vector3Normalize(Vector3Add(ctx->lightDir, viewDir));
                        float dotNH = fmaxf(Vector3DotProduct(n, halfDir), 0.0f);
                        float spec = powf(dotNH, 64.0f);

                        float ambient = 0.2f;
                        float intensity = fminf(ambient + diffuse * 0.6f + spec * 3.0f, 1.0f);

                        // Couleur diffuse + ambient
                        int r = (int)(base.r * intensity);
                        int g = (int)(base.g * intensity);
                        int b = (int)(base.b * intensity);

                        // Ajouter le specular BLANC par dessus
                        float specIntensity = spec * 1.5f;
                        r = (int)fminf(r + 255 * specIntensity, 255);
                        g = (int)fminf(g + 255 * specIntensity, 255);
                        b = (int)fminf(b + 255 * specIntensity, 255);

                        Color pixelColor = {(unsigned char)r, (unsigned char)g, (unsigned char)b, 255};

                        framebuffer[(SCREEN_HEIGHT - y) * SCREEN_WIDTH + x] = pixelColor;
                    }
                }

                // incrément X
                w0 += e0.A;
                w1 += e1.A;
                w2 += e2.A;
            }

            // incrément Y
            w0_row += e0.B;
            w1_row += e1.B;
            w2_row += e2.B;
        }
    }
}

static Color colorFromIndex(int i)
{
    return (Color){
        (unsigned char)((i * 97) % 255),
        (unsigned char)((i * 57) % 255),
        (unsigned char)((i * 23) % 255),
        120 // alpha pour transparence
    };
}

void debugDrawTileCoverage(RenderContext* ctx)
{
    for (int i = 0; i < ctx->polyCount; i++)
    {
        Poly* p = &ctx->polys[i];

        // Permet de savoir où ranger le triangle 
        // pixel / TILE_SIZE = tile index
        //int minTileX = (int)(p->minX) / TILE_SIZE;
        //int maxTileX = (int)(p->maxX) / TILE_SIZE;
        //int minTileY = (int)(p->minY) / TILE_SIZE;
        //int maxTileY = (int)(p->maxY) / TILE_SIZE;

        int minTileX = Clamp(p->tileMinX, 0, tilesX-1);
        int maxTileX = Clamp(p->tileMaxX, 0, tilesX-1);
        int minTileY = Clamp(p->tileMinY, 0, tilesY-1);
        int maxTileY = Clamp(p->tileMaxY, 0, tilesY-1);

        Color col = colorFromIndex(i);

        for (int ty = minTileY; ty <= maxTileY; ty++)
        {
            for (int tx = minTileX; tx <= maxTileX; tx++)
            {
                int x = tx * TILE_SIZE;
                int y = ty * TILE_SIZE;

                DrawRectangle(
                    x,
                    SCREEN_HEIGHT - (y + TILE_SIZE),
                    TILE_SIZE,
                    TILE_SIZE,
                    col
                );
            }
        }
    }
}

int compare_zmin(const void *a, const void *b) {
    Poly *p1 = (Poly*)a;
    Poly *p2 = (Poly*)b;
    if (p1->zmin < p2->zmin) return -1;
    if (p1->zmin > p2->zmin) return  1;
    return 0;
}

// Fonction worker
void* worker(void* arg) {
    ThreadData* td = (ThreadData*)arg;

    while (true) {
        // Attendre le signal de départ
        pthread_barrier_wait(&barrierStart);

        for (int t = td->startTile; t < td->endTile; t++) {
            int tx = t % tilesX;
            int ty = t / tilesX;

        for (int row = 0; row < TILE_SIZE; row++) {
            int y = ty * TILE_SIZE + row;           // y monde comme dans drawTile
            int screenY = SCREEN_HEIGHT - y;        // même calcul que drawTile
            
            if (screenY < 0 || screenY >= SCREEN_HEIGHT) continue;
            
            memset(
                &framebuffer[screenY * SCREEN_WIDTH + tx * TILE_SIZE],
                0,
                TILE_SIZE * sizeof(Color)
            );
        }

            drawTile(td->ctx, tx, ty);
        }

        // Signaler que ce thread a fini
        pthread_barrier_wait(&barrierEnd);
    }
    return NULL;
}

void initThreads(RenderContext* ctx) {
    int tilesPerThread = (tilesX * tilesY + NUM_THREADS - 1) / NUM_THREADS;

    for (int i = 0; i < NUM_THREADS; i++) {
        threadData[i].ctx = ctx;
        threadData[i].startTile = i * tilesPerThread;
        threadData[i].endTile = (i + 1) * tilesPerThread;
        if (threadData[i].endTile > tilesX * tilesY)
            threadData[i].endTile = tilesX * tilesY;
        threadData[i].frameReady = false;
        pthread_mutex_init(&threadData[i].mutex, NULL);
        pthread_cond_init(&threadData[i].cond, NULL);

        pthread_create(&threads[i], NULL, worker, &threadData[i]);
    }
}

void renderFrame(RenderContext* ctx) {
    for (int i = 0; i < NUM_THREADS; i++)
        threadData[i].ctx = ctx;

    // Libérer tous les threads
    pthread_barrier_wait(&barrierStart);

    // Attendre que tous aient fini
    pthread_barrier_wait(&barrierEnd);
}


/***********************************************************************************************************/

Vector3 getNormal(Mesh mesh, int index, Vector3* smoothNormals) {
    /*if (mesh.normals)  // normales du fichier si disponibles
        return (Vector3){
            mesh.normals[index*3 + 0],
            mesh.normals[index*3 + 1],
            mesh.normals[index*3 + 2]
        };
    else // normales calculées sinon*/
        return smoothNormals[index];
}

Vector3 getVertex(Mesh mesh, int index) {
    return (Vector3){
        mesh.vertices[index*3 + 0],
        mesh.vertices[index*3 + 1],
        mesh.vertices[index*3 + 2]
    };
}

// Fonction helper
Vector2 getUV(Mesh mesh, int index) {
    if (!mesh.texcoords) return (Vector2){0, 0};
    return (Vector2){
        mesh.texcoords[index*2 + 0],
        mesh.texcoords[index*2 + 1]
    };
}

int main(void)
{
#if WARNOCK
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Hybride : Warnock + ZBuffer rendering");
#endif

#if ZBUFFER
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "ZBuffer rendering");
#endif

#if TILES
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Tiles rendering");
#endif

    Camera3D camera = { 0 };
    //camera.position = (Vector3){ 10.0f, -10.0f, 140.0f }; // pour tankTri
    //camera.position = (Vector3){ 10.0f, 10.0f, 10.0f }; // pour teapot
    camera.position = (Vector3){ 5.0f, 5.0f, 5.0f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, -1.0f, 0.0f };
    camera.fovy = 25.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    //Model model = LoadModel("suzane.obj");
    Model model = LoadModel("susaneHiDef.obj");
    //Model model = LoadModel("cube.obj");
    //Model model = LoadModel("teapot.obj");
    //Model model = LoadModel("teapotUV.obj");
    //Model model = LoadModel("tankTri.obj");
    //Model model = LoadModel("donut.obj");
    //Model model = LoadModel("donutSimple.obj");
    Mesh mesh = model.meshes[0]; // On prend le premier mesh

    Vector3 *vertices = (Vector3 *)mesh.vertices;
    int vertexCount = mesh.vertexCount;

    float rotX = 0.0f, rotY = 0.0f, rotZ = 0.0f;

    SetTargetFPS(60);

    RenderContext ctx;
    ctx.lightDir = Vector3Normalize((Vector3){ 1.0f, 1.0f, -1.0f });
    ctx.cameraPos = camera.position;
    ctx.texImage = LoadImage("rusty_metal_02_diff_1k.jpg");
    ctx.normalMap = LoadImage("rusty_metal_02_nor_dx_1k.jpg");

    // Tableau de normales par sommet
    Vector3* smoothNormals = calloc(vertexCount, sizeof(Vector3));
    Vector2* projected = malloc(vertexCount * sizeof(Vector2));
    float* z = malloc(vertexCount * sizeof(float));
    Poly* PolyList = malloc(mesh.vertexCount/3 * sizeof(Poly));
    Poly *visiblePolys = malloc(mesh.triangleCount * sizeof(Poly));

    for (int i = 0; i < mesh.vertexCount / 3; i++) {
        Poly p;
        p.visible = false;
        p.couleur = couleurAleatoire();
        PolyList[i] = p;
    }

#if ZBUFFER
    float* zbuffer = malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(float));
#endif

#if TILES
    Image img = GenImageColor(SCREEN_WIDTH, SCREEN_HEIGHT, RAYWHITE);
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);

    // Init
    pthread_barrier_init(&barrierStart, NULL, NUM_THREADS + 1);
    pthread_barrier_init(&barrierEnd,   NULL, NUM_THREADS + 1);

    initThreads(&ctx);
#endif

    // PASSE 1 : calculer une normale par triangle et l'accumuler sur ses sommets
    Vector3* faceNormals = calloc(vertexCount, sizeof(Vector3));

    for (int i = 0; i < mesh.triangleCount; i++) {
        int i0 = i*3, i1 = i*3+1, i2 = i*3+2;

        Vector3 v0 = getVertex(mesh, i0);
        Vector3 v1 = getVertex(mesh, i1);
        Vector3 v2 = getVertex(mesh, i2);

        Vector3 e1 = Vector3Subtract(v1, v0);
        Vector3 e2 = Vector3Subtract(v2, v0);
        Vector3 n  = Vector3CrossProduct(e1, e2);

        faceNormals[i0] = Vector3Add(faceNormals[i0], n);
        faceNormals[i1] = Vector3Add(faceNormals[i1], n);
        faceNormals[i2] = Vector3Add(faceNormals[i2], n);
    }

    // PASSE 2 : fusionner par position identique
    printf("Calcul smooth normals...\n");
    for (int i = 0; i < vertexCount; i++) {
        Vector3 vi = getVertex(mesh, i);
        Vector3 accumulated = {0, 0, 0};

        for (int j = 0; j < vertexCount; j++) {
            Vector3 vj = getVertex(mesh, j);

            if (fabsf(vi.x - vj.x) < 0.0001f &&
                fabsf(vi.y - vj.y) < 0.0001f &&
                fabsf(vi.z - vj.z) < 0.0001f)
            {
                accumulated = Vector3Add(accumulated, faceNormals[j]);
            }
        }

        smoothNormals[i] = Vector3Normalize(accumulated);
    }

    free(faceNormals);

    while (!WindowShouldClose())
    {
        //UpdateCamera(&camera, CAMERA_ORBITAL);
        ctx.cameraPos = camera.position;

        rotX += 0.01f;
        rotY -= 0.015f;
        rotZ += 0.015f;

        //rotX = fmodf(rotX + 0.01f, 2.0f * PI);
        //rotY = fmodf(rotY + 0.005f, 2.0f * PI);
        //rotZ = fmodf(rotZ + 0.015f, 2.0f * PI);

        Matrix rotation = MatrixRotateXYZ((Vector3){ rotX, rotY, rotZ });
        //Matrix rotation = MatrixRotateXYZ((Vector3){ 0, rotY, 0 });

        BeginDrawing();
        ClearBackground(BLACK);

        for (int i = 0; i < vertexCount; i++) {
            Vector3 v = getVertex(mesh, i);
            v = Vector3Transform(v, rotation);
            projected[i] = GetWorldToScreen(v, camera);

            Matrix view = GetCameraMatrix(camera);
            Vector3 viewPos = Vector3Transform(v, view);

            z[i] = viewPos.z;
            //z[i] = v.z;
        }
        
        for (int i = 0; i < mesh.vertexCount / 3; i++) {
            int i0 = i * 3;
            int i1 = i * 3 + 1;
            int i2 = i * 3 + 2;

            Poly* p = &PolyList[i];

            p->p0 = projected[i0];
            p->p1 = projected[i1];
            p->p2 = projected[i2];

            p->z0 = z[i0];
            p->z1 = z[i1];
            p->z2 = z[i2];

            p->zmin = fminf(p->z0, fminf(p->z1, p->z2));
            p->zmax = fmaxf(p->z0, fmaxf(p->z1, p->z2));

            float minX = fminf(p->p0.x, fminf(p->p1.x, p->p2.x));
            float maxX = fmaxf(p->p0.x, fmaxf(p->p1.x, p->p2.x));
            float minY = fminf(p->p0.y, fminf(p->p1.y, p->p2.y));
            float maxY = fmaxf(p->p0.y, fmaxf(p->p1.y, p->p2.y));

            // écran
            p->minX = fmaxf(0.0f, minX);
            p->maxX = fminf(SCREEN_WIDTH-1, maxX);
            p->minY = fmaxf(0.0f, minY);
            p->maxY = fminf(SCREEN_HEIGHT-1, maxY);

            if (p->maxX < 0 || p->minX > SCREEN_WIDTH-1 ||
                p->maxY < 0 || p->minY > SCREEN_HEIGHT-1)
            {
                p->visible = false;
                continue;
            }
            p->visible = true;

            p->tileMinX = (int)(p->minX) >> 5; // equivalent à (int)(p->minX) / TILE_SIZE avec TILE_SIZE ==> (32= 2^5);
            p->tileMaxX = (int)(p->maxX) >> 5;
            p->tileMinY = (int)(p->minY) >> 5;
            p->tileMaxY = (int)(p->maxY) >> 5;

            p->uv0 = getUV(mesh, i0);
            p->uv1 = getUV(mesh, i1);
            p->uv2 = getUV(mesh, i2);

            // Calcul tangent/bitangent à partir des UVs
            Vector3 edge1 = Vector3Subtract(p->v1, p->v0);
            Vector3 edge2 = Vector3Subtract(p->v2, p->v0);

            float duv1x = p->uv1.x - p->uv0.x;
            float duv1y = p->uv1.y - p->uv0.y;
            float duv2x = p->uv2.x - p->uv0.x;
            float duv2y = p->uv2.y - p->uv0.y;

            float f = 1.0f / (duv1x * duv2y - duv2x * duv1y);

            p->tangent = Vector3Normalize((Vector3){
                f * (duv2y * edge1.x - duv1y * edge2.x),
                f * (duv2y * edge1.y - duv1y * edge2.y),
                f * (duv2y * edge1.z - duv1y * edge2.z)
            });

            p->bitangent = Vector3Normalize((Vector3){
                f * (-duv2x * edge1.x + duv1x * edge2.x),
                f * (-duv2x * edge1.y + duv1x * edge2.y),
                f * (-duv2x * edge1.z + duv1x * edge2.z)
            });
        }

        Matrix view = GetCameraMatrix(camera);
        int inc = 0;

        for (int i = 0; i < mesh.triangleCount; i++)
        {
            int i0 = i * 3;
            int i1 = i * 3 + 1;
            int i2 = i * 3 + 2;

            Vector3 v0 = Vector3Transform(getVertex(mesh, i0), rotation);
            Vector3 v1 = Vector3Transform(getVertex(mesh, i1), rotation);
            Vector3 v2 = Vector3Transform(getVertex(mesh, i2), rotation);

            // passer en espace caméra
            v0 = Vector3Transform(v0, view);
            v1 = Vector3Transform(v1, view);
            v2 = Vector3Transform(v2, view);

            Vector3 e1 = Vector3Subtract(v1, v0);
            Vector3 e2 = Vector3Subtract(v2, v0);

            Vector3 normal = Vector3CrossProduct(e1, e2);
            normal = Vector3Normalize(normal);

            float intensity = Vector3DotProduct(normal, ctx.lightDir);
            float ambient = 0.2f;
            intensity = fmaxf(intensity, ambient);

            if (intensity < 0) intensity = 0;
#if BACKFACECULLING
            if (normal.z < 0)
            {
#endif
                Poly p = PolyList[i];

                // couleur de base (blanc ou autre)
                //Color base = WHITE;
                Color base = PolyList[i].couleur;
                //Color base = {200, 100, 50, 255}; // cuivre / terre cuite
                p.couleur = base;
#if !TILES
                p.couleur = (Color){
                    (unsigned char)(base.r * intensity),
                    (unsigned char)(base.g * intensity),
                    (unsigned char)(base.b * intensity),
                    255
                };
#endif

                // Transformer les normales comme les vertices
                p.n0 = Vector3Normalize(Vector3Transform(getNormal(mesh, i0, smoothNormals), rotation));
                p.n1 = Vector3Normalize(Vector3Transform(getNormal(mesh, i1, smoothNormals), rotation));
                p.n2 = Vector3Normalize(Vector3Transform(getNormal(mesh, i2, smoothNormals), rotation));

                p.v0 = Vector3Transform(getVertex(mesh, i0), rotation);
                p.v1 = Vector3Transform(getVertex(mesh, i1), rotation);
                p.v2 = Vector3Transform(getVertex(mesh, i2), rotation);

                p.tangent   = Vector3Normalize(Vector3Transform(p.tangent,   rotation));
                p.bitangent = Vector3Normalize(Vector3Transform(p.bitangent, rotation));

                visiblePolys[inc++] = p;
#if BACKFACECULLING
            }
#endif
        }
        int polyCount = inc;

#if WARNOCK
        DrawText("Hybride : Warnock + ZBuffer", 10, 10, 20, WHITE);
        DrawText(TextFormat("Profondeur de l'arbre = %d | FPS = %d", TREE_DEPTH, GetFPS()),
         10, 40, 20, WHITE);
        DrawText(TextFormat("Nombre de triangles = %d", polyCount),
         10, 70, 20, WHITE);

        Region root = {0,0,SCREEN_WIDTH,SCREEN_HEIGHT};
        int indices[MAX_POLY];
        for (int i = 0; i < polyCount; i++)
            indices[i] = i;
        //RenderContext ctx;
        ctx.polys = visiblePolys;
        ctx.polyCount=polyCount;
        ctx.rootIndices = indices;
        warnock(&ctx, &root, indices, polyCount, 0);
#endif

#if ZBUFFER
        DrawText("ZBuffer", 10, 10, 20, WHITE);
        DrawText(TextFormat("FPS = %d", GetFPS()),
         10, 40, 20, WHITE);
        DrawText(TextFormat("Nombre de triangles = %d", polyCount),
         10, 70, 20, WHITE);

        for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++)
            zbuffer[i] = 1e9; // très loin

        for (int i = 0; i < polyCount; i++)
        {
            drawTriangleZ(&visiblePolys[i], zbuffer);
        }
#endif

#if TILES
        ctx.polys = visiblePolys;
        ctx.polyCount = polyCount;

        qsort(ctx.polys, ctx.polyCount, sizeof(Poly), compare_zmin);

        for (int i = 0; i < tilesX * tilesY; i++){
            tiles[i].minZ = 1e9f;
            tiles[i].maxZ = -1e9f;
        }

        if (IsKeyDown(KEY_UP))    camera.position.y += 10.0f;
        if (IsKeyDown(KEY_DOWN))  camera.position.y -= 10.0f;
        if (IsKeyDown(KEY_RIGHT)) camera.position.x += 10.0f;
        if (IsKeyDown(KEY_LEFT))  camera.position.x -= 10.0f;
        if (IsKeyDown(KEY_W))     camera.position.z -= 10.0f;
        if (IsKeyDown(KEY_S))     camera.position.z += 10.0f;

        binTriangles(&ctx);

        renderFrame(&ctx);
        UpdateTexture(tex, framebuffer);
        DrawTexture(tex, 0, 0, WHITE);

#if DEBUG_TILES
        /*** DEBUG TILES ***/
/*
        // debug : tiles avec triangles
        for (int ty = 0; ty < tilesY; ty++)
        {
            for (int tx = 0; tx < tilesX; tx++)
            {
                Tile* tile = &tiles[ty * tilesX + tx];

                if (tile->count == 0) continue;

                int x = tx * TILE_SIZE;
                int y = ty * TILE_SIZE;

                DrawRectangleLines(x, SCREEN_HEIGHT - (y + TILE_SIZE), TILE_SIZE, TILE_SIZE, GREEN);
            }
        }
*/
        // debug : densité par tiles
        for (int ty = 0; ty < tilesY; ty++)
        {
            for (int tx = 0; tx < tilesX; tx++)
            {
                Tile* tile = &tiles[ty * tilesX + tx];

                //if (tile->count == 0) continue;

                int x = tx * TILE_SIZE;
                int y = ty * TILE_SIZE;

                int c = tile->count + 17;

                unsigned char alpha = (unsigned char)Clamp(c * 10, 20, 200);

                Color col = (Color){
                    (unsigned char)(c * 5),  // rouge ↑ avec densité
                    0,
                    0,
                    alpha
                };

                Color whiteText = (Color){
                    (unsigned char)(c * 5),  // blanc ↑ avec densité
                    (unsigned char)(c * 5),
                    (unsigned char)(c * 5),
                    alpha
                };

                DrawRectangleLinesEx((Rectangle){x, SCREEN_HEIGHT - (y + TILE_SIZE), TILE_SIZE, TILE_SIZE},1.5f, col);
                DrawText(TextFormat("%d", tile->count), x+2, SCREEN_HEIGHT - y - 14, 10, whiteText);
            }
        }

        //debugDrawTileCoverage(&ctx);
#endif

        DrawText("Tiles", 10, 10, 20, WHITE);
        DrawText(TextFormat("FPS = %d", GetFPS()),
         10, 40, 20, WHITE);
        DrawText(TextFormat("Nombre de triangles = %d", polyCount),
         10, 70, 20, WHITE);

        // Afficher la position pour la noter
        DrawText(TextFormat("pos: %.1f %.1f %.1f", 
            camera.position.x, camera.position.y, camera.position.z), 
            10, 100, 20, RED);
#endif

        EndDrawing();
    }

    free(smoothNormals);
    free(projected);
    free(z);
    free(PolyList);
    free(visiblePolys);

    UnloadModel(model);
    if (ctx.texImage.data)  UnloadImage(ctx.texImage);
    if (ctx.normalMap.data) UnloadImage(ctx.normalMap);

    CloseWindow();
#if ZBUFFER
    free(zbuffer);
#endif
    return 0;
}