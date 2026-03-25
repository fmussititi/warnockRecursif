#include "raylib.h"
#include "raymath.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"
#include <stdatomic.h>
#include <pthread.h>
#include <immintrin.h>
#include "config.h"

#pragma region Paramètres généraux et Variables globales

// Parametres pour Tiles
#define MAX_TRI_PER_TILE 20000
#define MAX_TILES 2000

int tilesX;
int tilesY;
atomic_int tilesRemaining;
// Buffer global
Color* framebuffer;

ThreadData* threadData;
pthread_t* threads;
pthread_barrier_t barrierStart;  // main attend que les threads soient prêts
pthread_barrier_t barrierEnd;    // main attend que les threads aient fini
pthread_barrier_t barrierSkyboxDone;

typedef struct {
    int count;
    int indices[MAX_TRI_PER_TILE];
    float minZ;
    float maxZ;
} Tile;

Tile tiles[MAX_TILES];

#pragma endregion


#pragma region Fonctions divers
/***********************************************************************************************************/
/******************************************* Fonctions Divers **********************************************/

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

int compare_zmin(const void *a, const void *b) {
    Poly *p1 = (Poly*)a;
    Poly *p2 = (Poly*)b;
    if (p1->zmin < p2->zmin) return -1;
    if (p1->zmin > p2->zmin) return  1;
    return 0;
}

Color SampleEquirectangular(RenderContext *ctx, Vector3 dir)
{
    if (!ctx->envMap.data) return (Color){255,0,255,255};

    dir = Vector3Normalize(dir);

    float u = 0.5f + atan2f(dir.z, dir.x) / (2.0f * PI);
    float v = 0.5f - asinf(dir.y) / PI;

    int x = (int)(u * ctx->envMap.width);
    int y = (int)(v * ctx->envMap.height);

    // wrap horizontal (important)
    x = (x % ctx->envMap.width + ctx->envMap.width) % ctx->envMap.width;

    // clamp vertical
    if (y < 0) y = 0;
    if (y >= ctx->envMap.height) y = ctx->envMap.height - 1;
    
    Color* pixels = (Color*)ctx->envMap.data;
    return pixels[y * ctx->envMap.width + x];
    //return GetImageColor(ctx->envMap, x, y);
}

Vector3 GetRayDirection(RenderContext *ctx, int px, int py)
{
    float ndcX = (2.0f * px / ctx->screenWidth  - 1.0f);
    float ndcY = (1.0f - 2.0f * py / ctx->screenHeight);

    float fov = ctx->fov * DEG2RAD;
    float aspect = (float)ctx->screenWidth / ctx->screenHeight;

    float sx = ndcX * tanf(fov * 0.5f) * aspect;
    float sy = ndcY * tanf(fov * 0.5f);

    Vector3 dir =
        Vector3Add(
            ctx->cameraForward,
            Vector3Add(
                Vector3Scale(ctx->cameraRight, sx),
                Vector3Scale(ctx->cameraUp, sy)
            )
        );

    return Vector3Normalize(dir);
}

void PrecomputeSkyboxLUT(RenderContext* ctx)
{
    int W = ctx->screenWidth;
    int H = ctx->screenHeight;

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {

            // 1) Direction caméra → pixel
            Vector3 dir = GetRayDirection(ctx, x, y);

            // 2) Normalisation
            dir = Vector3Normalize(dir);

            // 3) Conversion en UV équirectangulaire
            float u = 0.5f + atan2f(dir.z, dir.x) / (2.0f * PI);
            float v = 0.5f - asinf(dir.y) / PI;

            int idx = y * W + x;
            ctx->skyU[idx] = u;
            ctx->skyV[idx] = v;
        }
    }
}

static inline Color SampleEquirectangularUV(RenderContext* ctx, float u, float v)
{
    int x = (int)(u * ctx->envMap.width);
    int y = (int)(v * ctx->envMap.height);

    x = (x % ctx->envMap.width + ctx->envMap.width) % ctx->envMap.width;
    y = Clamp(y, 0, ctx->envMap.height - 1);

    Color* pixels = (Color*)ctx->envMap.data;
    return pixels[y * ctx->envMap.width + x];
    //return GetImageColor(ctx->envMap, x, y);
}

void flatShading(RenderContext* ctx, Poly* p, Matrix view)
{
    // passer en espace caméra
    Vector3 v0 = Vector3Transform(p->v0, view);
    Vector3 v1 = Vector3Transform(p->v1, view);
    Vector3 v2 = Vector3Transform(p->v2, view);

    Vector3 e1 = Vector3Subtract(v1, v0);
    Vector3 e2 = Vector3Subtract(v2, v0);

    Vector3 normal = Vector3CrossProduct(e1, e2);
    normal = Vector3Normalize(normal);

    float intensity = Vector3DotProduct(normal, ctx->lightDir);
    float ambient = ctx->ambient;
    intensity = fmaxf(intensity, ambient);

    p->couleur = (Color){
                    (unsigned char)(p->couleur.r * intensity),
                    (unsigned char)(p->couleur.g * intensity),
                    (unsigned char)(p->couleur.b * intensity),
                    255
                };
}

void gouraudShading(RenderContext* ctx, Poly* p)
{
    // Calcul de l'intensité lumineuse pour chaque sommet
    // (les normales sont déjà transformées dans p->n0/n1/n2)

    float i0 = fmaxf(Vector3DotProduct(p->n0, ctx->lightDir), ctx->ambient);
    float i1 = fmaxf(Vector3DotProduct(p->n1, ctx->lightDir), ctx->ambient);
    float i2 = fmaxf(Vector3DotProduct(p->n2, ctx->lightDir), ctx->ambient);

    p->c0 = (Color){
        (unsigned char)(p->couleur.r * i0),
        (unsigned char)(p->couleur.g * i0),
        (unsigned char)(p->couleur.b * i0),
        255
    };
    p->c1 = (Color){
        (unsigned char)(p->couleur.r * i1),
        (unsigned char)(p->couleur.g * i1),
        (unsigned char)(p->couleur.b * i1),
        255
    };
    p->c2 = (Color){
        (unsigned char)(p->couleur.r * i2),
        (unsigned char)(p->couleur.g * i2),
        (unsigned char)(p->couleur.b * i2),
        255
    };
}

#pragma endregion 


#pragma region Warnock
/***********************************************************************************************************/
//******************************** Fonctions pour Warnock rendering ****************************************/

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

void drawRegionZBuffer(RenderContext* ctx, Region* r, Poly* polys, int* indices, int count)
{
    int width  = r->x2 - r->x1;
    int height = r->y2 - r->y1;

    float zbuf[ctx->tile_size * ctx->tile_size];

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
                        DrawRectangle(x, ctx->screenHeight - y, width+1, height+1, tri->couleur);
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
    int top = ctx->screenHeight - r->y2;
    int width = r->x2 - r->x1;
    int height = r->y2 - r->y1;

    if (depth >= ctx->tree_depth)
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
        drawRegionZBuffer(ctx, r, ctx->polys, indices, count);
        if (ctx->contour_arbre){
            //DrawRectangleLines(left, top, width, height, ctx->polys[indices[best]].couleur);
        }

        return;
    }

    int localIndices[ctx->max_poly];
    int localCount = 0;

    for (int i = 0; i < count; i++)
    {
        int idx = indices[i];

        if (localCount >= ctx->max_poly)
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
        if (ctx->tree_depth){
            DrawRectangleLines(left, top, width, height, RED);
        }
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
#pragma endregion


#pragma region Zbuffer
/***********************************************************************************************************/
/*************************************** Fonction pour ZBuffer rendering ***********************************/

//** Dessin d'un triangle dans le zbuffer **/
void drawTriangleZ(RenderContext* ctx, Poly* tri, float* zbuffer)
{
    // bounding box écran
    int minX = fmaxf(0, floorf(fminf(tri->p0.x, fminf(tri->p1.x, tri->p2.x))));
    int maxX = fminf(ctx->screenWidth-1, ceilf(fmaxf(tri->p0.x, fmaxf(tri->p1.x, tri->p2.x))));
    int minY = fmaxf(0, floorf(fminf(tri->p0.y, fminf(tri->p1.y, tri->p2.y))));
    int maxY = fminf(ctx->screenHeight-1, ceilf(fmaxf(tri->p0.y, fmaxf(tri->p1.y, tri->p2.y))));

    for (int y = minY; y <= maxY; y++)
    {
        for (int x = minX; x <= maxX; x++)
        {
            Vector2 p = {x + 0.5f, y + 0.5f};

            if (CheckCollisionPointTriangle(p, tri->p0, tri->p1, tri->p2))
            {
                // interpolation simple du z (approx)
                // float z = (tri->z0 + tri->z1 + tri->z2) / 3.0f;

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


                int index = y * ctx->screenWidth + x;

                if (z < zbuffer[index])
                {
                    zbuffer[index] = z;

                    // ← Interpolation Gouraud
                    Color final = (Color){
                        (unsigned char)(w0*tri->c0.r + w1*tri->c1.r + w2*tri->c2.r),
                        (unsigned char)(w0*tri->c0.g + w1*tri->c1.g + w2*tri->c2.g),
                        (unsigned char)(w0*tri->c0.b + w1*tri->c1.b + w2*tri->c2.b),
                        255
                    };

                    DrawPixel(x, ctx->screenHeight - y, final);
                }
            }
        }
    }
}
#pragma endregion


#pragma region Tiles
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
                    tx * ctx->tile_size,
                    ty * ctx->tile_size,
                    (tx+1) * ctx->tile_size,
                    (ty+1) * ctx->tile_size
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
    int x0 = tx * ctx->tile_size;
    int y0 = ty * ctx->tile_size;
    int x1 = x0 + ctx->tile_size;
    int yEnd = y0 + ctx->tile_size;

    Tile* tile = &tiles[ty * tilesX + tx];

    if (tile->count == 0) return;

    float zbuf[ctx->tile_size * ctx->tile_size];
    for (int i = 0; i < ctx->tile_size * ctx->tile_size; i++)
        zbuf[i] = 1e9f;

    // ← Précalcul des flags et pointeurs, une seule fois pour toute la tile
    const bool hasTexture   = ctx->texImage.data   != NULL;
    const bool hasNormalMap = ctx->normalMap.data  != NULL;
    const bool hasEnvMap    = ctx->envMap.data     != NULL;

    const Color* texPixels    = hasTexture   ? (Color*)ctx->texImage.data   : NULL;
    const Color* normalPixels = hasNormalMap ? (Color*)ctx->normalMap.data  : NULL;
    const int texW    = ctx->texImage.width;
    const int texH    = ctx->texImage.height;
    const int normW   = ctx->normalMap.width;
    const int normH   = ctx->normalMap.height;
    const int envW    = ctx->envMap.width;
    const int envH    = ctx->envMap.height;

    for (int t = 0; t < tile->count; t++)
    {
        Poly* tri = &ctx->polys[tile->indices[t]];

        int minX = (int)fmaxf(x0, floorf(tri->minX));
        int maxX = (int)fminf(x1 - 1, ceilf(tri->maxX));
        int minY = (int)fmaxf(y0, floorf(tri->minY));
        int maxY = (int)fminf(yEnd-1, ceilf(tri->maxY));

        if (minX > maxX || minY > maxY) continue;

        EdgeEq e0 = makeEdge(tri->p0, tri->p1);
        EdgeEq e1 = makeEdge(tri->p1, tri->p2);
        EdgeEq e2 = makeEdge(tri->p2, tri->p0);

        float area    = (tri->p1.x - tri->p0.x)*(tri->p2.y - tri->p0.y) -
                        (tri->p2.x - tri->p0.x)*(tri->p1.y - tri->p0.y);
        float invArea = 1.0f / area;

        float startX = minX + 0.5f;
        float startY = minY + 0.5f;

        float w0_row = evalEdge(e0, startX, startY);
        float w1_row = evalEdge(e1, startX, startY);
        float w2_row = evalEdge(e2, startX, startY);

        __m256 e0A_8 = _mm256_set1_ps(e0.A * 8);
        __m256 e1A_8 = _mm256_set1_ps(e1.A * 8);
        __m256 e2A_8 = _mm256_set1_ps(e2.A * 8);
        __m256 offsets = _mm256_set_ps(7,6,5,4,3,2,1,0);
        __m256 zero    = _mm256_setzero_ps();

        for (int y = minY; y <= maxY; y++)
        {
            __m256 w0_v = _mm256_add_ps(_mm256_set1_ps(w0_row), _mm256_mul_ps(offsets, _mm256_set1_ps(e0.A)));
            __m256 w1_v = _mm256_add_ps(_mm256_set1_ps(w1_row), _mm256_mul_ps(offsets, _mm256_set1_ps(e1.A)));
            __m256 w2_v = _mm256_add_ps(_mm256_set1_ps(w2_row), _mm256_mul_ps(offsets, _mm256_set1_ps(e2.A)));

            for (int x = minX; x <= maxX; x += 8)
            {
                __m256 m0 = _mm256_cmp_ps(w0_v, zero, _CMP_GE_OS);
                __m256 m1 = _mm256_cmp_ps(w1_v, zero, _CMP_GE_OS);
                __m256 m2 = _mm256_cmp_ps(w2_v, zero, _CMP_GE_OS);
                __m256 mask = _mm256_and_ps(_mm256_and_ps(m0, m1), m2);
                int imask = _mm256_movemask_ps(mask);

                if (imask != 0)
                {
                    float w0_arr[8], w1_arr[8], w2_arr[8];
                    _mm256_storeu_ps(w0_arr, w0_v);
                    _mm256_storeu_ps(w1_arr, w1_v);
                    _mm256_storeu_ps(w2_arr, w2_v);

                    for (int k = 0; k < 8; k++)
                    {
                        if (!(imask & (1 << k))) continue;
                        int px = x + k;
                        if (px > maxX) break;

                        float alpha = w1_arr[k] * invArea;
                        float beta  = w2_arr[k] * invArea;
                        float gamma = 1.0f - alpha - beta;

                        float z = alpha * tri->z0 + beta * tri->z1 + gamma * tri->z2;

                        int lx = px - x0;
                        int ly = y  - y0;
                        int index = ly * ctx->tile_size + lx;

                        if (z < zbuf[index])
                        {
                            zbuf[index] = z;

                            // Interpolation normale
                            Vector3 n = {
                                alpha * tri->n0.x + beta * tri->n1.x + gamma * tri->n2.x,
                                alpha * tri->n0.y + beta * tri->n1.y + gamma * tri->n2.y,
                                alpha * tri->n0.z + beta * tri->n1.z + gamma * tri->n2.z,
                            };
                            n = Vector3Normalize(n);
                            n = Vector3Negate(n);

                            // Position 3D interpolée
                            Vector3 pixelPos = {
                                alpha * tri->v0.x + beta * tri->v1.x + gamma * tri->v2.x,
                                alpha * tri->v0.y + beta * tri->v1.y + gamma * tri->v2.y,
                                alpha * tri->v0.z + beta * tri->v1.z + gamma * tri->v2.z,
                            };

                            // UV
                            float u = alpha * tri->uv0.x + beta * tri->uv1.x + gamma * tri->uv2.x;
                            float v = alpha * tri->uv0.y + beta * tri->uv1.y + gamma * tri->uv2.y;

                            // ← Normal map : pas de branche dans la boucle
                            if (hasNormalMap) {
                                int nx = (int)(u * normW) % normW;
                                int ny = (int)(v * normH) % normH;
                                if (nx < 0) nx += normW;
                                if (ny < 0) ny += normH;
                                Color nc = normalPixels[ny * normW + nx];
                                Vector3 nMap = {
                                    (nc.r / 255.0f) * 2.0f - 1.0f,
                                    (nc.g / 255.0f) * 2.0f - 1.0f,
                                    (nc.b / 255.0f) * 2.0f - 1.0f
                                };
                                Vector3 T = tri->tangent;
                                Vector3 B = tri->bitangent;
                                Vector3 N = n;
                                n = Vector3Normalize((Vector3){
                                    T.x*nMap.x + B.x*nMap.y + N.x*nMap.z,
                                    T.y*nMap.x + B.y*nMap.y + N.y*nMap.z,
                                    T.z*nMap.x + B.z*nMap.y + N.z*nMap.z
                                });
                            }

                            // ← Texture : pas de branche dans la boucle
                            Color base;
                            if (hasTexture) {
                                int texX = (int)(u * texW) % texW;
                                int texY = (int)(v * texH) % texH;
                                if (texX < 0) texX += texW;
                                if (texY < 0) texY += texH;
                                base = texPixels[texY * texW + texX];
                            } else {
                                base = tri->couleur;
                            }

                            // Lighting
                            Vector3 viewDir = Vector3Normalize(Vector3Subtract(ctx->cameraPos, pixelPos));

                            // ← EnvMap : pas de branche dans la boucle
                            Color envColor = base;
                            if (hasEnvMap) {
                                Vector3 I = Vector3Negate(viewDir);
                                Vector3 R = Vector3Subtract(I, Vector3Scale(n, 2.0f * Vector3DotProduct(I, n)));
                                R = Vector3Normalize(R);
                                envColor = SampleEquirectangular(ctx, R);
                                envColor.r = (unsigned char)fminf(envColor.r * 1.3f + 20, 255);
                                envColor.g = (unsigned char)fminf(envColor.g * 1.3f + 20, 255);
                                envColor.b = (unsigned char)fminf(envColor.b * 1.3f + 20, 255);
                            }

                            float fresnel = powf(1.0f - fmaxf(Vector3DotProduct(n, viewDir), 0.0f), 5.0f);
                            float dotNL   = fmaxf(Vector3DotProduct(n, ctx->lightDir), 0.0f);
                            float diffuse = dotNL;
                            Vector3 halfDir = Vector3Normalize(Vector3Add(ctx->lightDir, viewDir));
                            float dotNH = fmaxf(Vector3DotProduct(n, halfDir), 0.0f);
                            float spec  = (dotNL > 0.0f) ? powf(dotNH, ctx->shininess) : 0.0f;
                            float lighting = fminf(ctx->ambient + diffuse * ctx->diffuse, 1.0f);

                            Color litBase = {
                                (unsigned char)(base.r * lighting),
                                (unsigned char)(base.g * lighting),
                                (unsigned char)(base.b * lighting),
                                255
                            };

                            // ← Mix envMap : pas de branche dans la boucle
                            Color final;
                            if (hasEnvMap) {
                                float reflectivity = ctx->refl1 + ctx->refl2 * fresnel;
                                final = (Color){
                                    (unsigned char)(litBase.r * (1.0f - reflectivity) + envColor.r * reflectivity),
                                    (unsigned char)(litBase.g * (1.0f - reflectivity) + envColor.g * reflectivity),
                                    (unsigned char)(litBase.b * (1.0f - reflectivity) + envColor.b * reflectivity),
                                    255
                                };
                            } else {
                                final = litBase;
                            }

                            float specIntensity = spec * ctx->specular;
                            final.r = (unsigned char)fminf(final.r + 255 * specIntensity, 255);
                            final.g = (unsigned char)fminf(final.g + 255 * specIntensity, 255);
                            final.b = (unsigned char)fminf(final.b + 255 * specIntensity, 255);

                            int fbY = ctx->screenHeight - y;
                            if (fbY >= 0 && fbY < ctx->screenHeight)
                                framebuffer[fbY * ctx->screenWidth + px] = final;
                        }
                    }
                }

                w0_v = _mm256_add_ps(w0_v, e0A_8);
                w1_v = _mm256_add_ps(w1_v, e1A_8);
                w2_v = _mm256_add_ps(w2_v, e2A_8);
            }

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
                int x = tx * ctx->tile_size;
                int y = ty * ctx->tile_size;

                DrawRectangle(
                    x,
                    ctx->screenHeight - (y + ctx->tile_size),
                    ctx->tile_size,
                    ctx->tile_size,
                    col
                );
            }
        }
    }
}

// Fonction worker
void* worker(void* arg) {
    ThreadData* td = (ThreadData*)arg;

    while (true) {

        // 1) Début de frame
        pthread_barrier_wait(&barrierStart);

        // 2) Phase SKYBOX (par lignes)
        if (!td->ctx->skyU || !td->ctx->skyV || !td->ctx->envMap.data) {
            // pas de LUT ou pas d'envMap → on saute la skybox
        } else {
            for (int y = td->startLine; y < td->endLine; y++) {
                if (y < 0 || y >= td->ctx->screenHeight) continue;

                int screenY = td->ctx->screenHeight - y;
                if (screenY < 0 || screenY >= td->ctx->screenHeight) continue;

                int base = screenY * td->ctx->screenWidth;
                int lutRow = y * td->ctx->screenWidth;

                for (int x = 0; x < td->ctx->screenWidth; x++) {
                    int idx = lutRow + x;
                    framebuffer[base + x] =
                        SampleEquirectangularUV(td->ctx,
                                                td->ctx->skyU[idx],
                                                td->ctx->skyV[idx]);
                }
            }

            if (td->ctx->blur) {
                // ← BLUR SKYBOX (sur les lignes du thread)
                for (int pass = 0; pass < td->ctx->pass; pass++) {
                    int radius = td->ctx->radius; // ajuster : 1=léger, 5=fort
                    for (int y = td->startLine; y < td->endLine; y++) {
                        int screenY = td->ctx->screenHeight - y;
                        if (screenY < 0 || screenY >= td->ctx->screenHeight) continue;

                        for (int x = 0; x < td->ctx->screenWidth; x++) {
                            int r = 0, g = 0, b = 0, count = 0;

                            for (int dy = -radius; dy <= radius; dy++) {
                                for (int dx = -radius; dx <= radius; dx++) {
                                    int ny = screenY + dy;
                                    int nx = x + dx;
                                    if (nx < 0 || nx >= td->ctx->screenWidth) continue;
                                    if (ny < 0 || ny >= td->ctx->screenHeight) continue;

                                    Color c = framebuffer[ny * td->ctx->screenWidth + nx];
                                    r += c.r; g += c.g; b += c.b;
                                    count++;
                                }
                            }

                            framebuffer[screenY * td->ctx->screenWidth + x] = (Color){
                                r / count, g / count, b / count, 255
                            };
                        }
                    }
                }
            }
        }

        // 3) Attendre que tous les threads aient fini la skybox
        pthread_barrier_wait(&barrierSkyboxDone);

        // 4) Phase TRIANGLES (par tiles)
        for (int t = td->startTile; t < td->endTile; t++) {
            int tx = t % tilesX;
            int ty = t / tilesX;

            if (!td->ctx->skyU || !td->ctx->skyV || !td->ctx->envMap.data){
                for (int row = 0; row < td->ctx->tile_size; row++) {
                    int y = ty * td->ctx->tile_size + row;           // y monde comme dans drawTile
                    int screenY = td->ctx->screenHeight - y;        // même calcul que drawTile
                    
                    if (screenY < 0 || screenY >= td->ctx->screenHeight) continue;
                    
                    memset(
                        &framebuffer[screenY * td->ctx->screenWidth + tx * td->ctx->tile_size],
                        0,
                        td->ctx->tile_size * sizeof(Color)
                    );
                }
            }

            // IMPORTANT : ne plus effacer la tile ici
            drawTile(td->ctx, tx, ty);
        }

        // 5) Fin de frame
        pthread_barrier_wait(&barrierEnd);
    }

    return NULL;
}

void initThreads(RenderContext* ctx) {

    int totalTiles = tilesX * tilesY;
    int tilesPerThread = (totalTiles + ctx->num_threads - 1) / ctx->num_threads;
    int linesPerThread = ctx->screenHeight / ctx->num_threads;

    for (int i = 0; i < ctx->num_threads; i++) {

        threadData[i].ctx = ctx;

        // --- Découpage TILES ---
        threadData[i].startTile = i * tilesPerThread;
        threadData[i].endTile   = (i == ctx->num_threads - 1)
                                    ? totalTiles
                                    : (i + 1) * tilesPerThread;

        // --- Découpage LIGNES ---
        threadData[i].startLine = i * linesPerThread;
        threadData[i].endLine   = (i == ctx->num_threads - 1)
                                    ? ctx->screenHeight
                                    : (i + 1) * linesPerThread;

        // (optionnel si tu n’utilises plus mutex/cond)
        threadData[i].frameReady = false;
        pthread_mutex_init(&threadData[i].mutex, NULL);
        pthread_cond_init(&threadData[i].cond, NULL);

        // --- Création du thread ---
        pthread_create(&threads[i], NULL, worker, &threadData[i]);
    }
}

void renderFrame(RenderContext* ctx) {
    for (int i = 0; i < ctx->num_threads; i++)
        threadData[i].ctx = ctx;

    // Début de frame → réveiller les workers
    pthread_barrier_wait(&barrierStart);

    // Attendre fin skybox
    pthread_barrier_wait(&barrierSkyboxDone);

    // Attendre fin triangles
    pthread_barrier_wait(&barrierEnd);
}
#pragma endregion


#pragma region Frustum Culling
/***********************************************************************************************************/
//*********************************** Gestion du frustum culling *******************************************/

typedef struct {
    Vector3 normal;
    float   d;
} Plane;

typedef struct {
    Plane planes[6]; // left, right, bottom, top, near, far
} Frustum;

// Extraire les plans depuis la matrice ViewProjection
Frustum extractFrustum(Matrix vp)
{
    Frustum f;

    // Left
    f.planes[0].normal = (Vector3){ vp.m3 + vp.m0, vp.m7 + vp.m4, vp.m11 + vp.m8 };
    f.planes[0].d      = vp.m15 + vp.m12;

    // Right
    f.planes[1].normal = (Vector3){ vp.m3 - vp.m0, vp.m7 - vp.m4, vp.m11 - vp.m8 };
    f.planes[1].d      = vp.m15 - vp.m12;

    // Bottom
    f.planes[2].normal = (Vector3){ vp.m3 + vp.m1, vp.m7 + vp.m5, vp.m11 + vp.m9 };
    f.planes[2].d      = vp.m15 + vp.m13;

    // Top
    f.planes[3].normal = (Vector3){ vp.m3 - vp.m1, vp.m7 - vp.m5, vp.m11 - vp.m9 };
    f.planes[3].d      = vp.m15 - vp.m13;

    // Near
    f.planes[4].normal = (Vector3){ vp.m3 + vp.m2, vp.m7 + vp.m6, vp.m11 + vp.m10 };
    f.planes[4].d      = vp.m15 + vp.m14;

    // Far
    f.planes[5].normal = (Vector3){ vp.m3 - vp.m2, vp.m7 - vp.m6, vp.m11 - vp.m10 };
    f.planes[5].d      = vp.m15 - vp.m14;

    // Normaliser les plans
    for (int i = 0; i < 6; i++) {
        float len = Vector3Length(f.planes[i].normal);
        f.planes[i].normal = Vector3Scale(f.planes[i].normal, 1.0f / len);
        f.planes[i].d     /= len;
    }

    return f;
}


// Retourne false si l'AABB est complètement hors du frustum
bool aabbInFrustum(Frustum* f, Vector3 aabbMin, Vector3 aabbMax)
{
    for (int i = 0; i < 6; i++)
    {
        Vector3 n = f->planes[i].normal;

        // Point le plus proche du plan dans la direction de la normale
        // = point "positif" de l'AABB
        Vector3 pPos = {
            n.x >= 0 ? aabbMax.x : aabbMin.x,
            n.y >= 0 ? aabbMax.y : aabbMin.y,
            n.z >= 0 ? aabbMax.z : aabbMin.z
        };

        // Si ce point est derrière le plan → AABB entièrement dehors
        float dist = Vector3DotProduct(n, pPos) + f->planes[i].d;
        if (dist < 0)
            return false;
    }
    return true; // AABB dans le frustum (ou partiellement)
}

// Test précis : les 3 sommets du triangle contre chaque plan
bool triangleInFrustum(Frustum* f, Vector3 v0, Vector3 v1, Vector3 v2)
{
    for (int i = 0; i < 6; i++)
    {
        Vector3 n = f->planes[i].normal;
        float d   = f->planes[i].d;

        // Distance de chaque sommet au plan
        float d0 = Vector3DotProduct(n, v0) + d;
        float d1 = Vector3DotProduct(n, v1) + d;
        float d2 = Vector3DotProduct(n, v2) + d;

        // Si les 3 sommets sont derrière ce plan → triangle dehors
        if (d0 < 0 && d1 < 0 && d2 < 0)
            return false;
    }
    return true;
}

// Construire la matrice de projection perspective
Matrix buildProjectionMatrix(RenderContext* ctx)
{
    float aspect = (float)ctx->screenWidth / (float)ctx->screenHeight;
    float fovy   = ctx->fov * DEG2RAD;
    float near   = ctx->near;
    float far    = ctx->far;

    return MatrixPerspective(fovy, aspect, near, far);
}

bool isBackFace(Vector3 v0, Vector3 v1, Vector3 v2, Vector3 cameraPos)
{
    Vector3 e1     = Vector3Subtract(v1, v0);
    Vector3 e2     = Vector3Subtract(v2, v0);
    Vector3 normal = Vector3CrossProduct(e1, e2);

    // Vecteur du triangle vers la caméra
    Vector3 toCamera = Vector3Subtract(cameraPos, v0);

    // Si la normale pointe dans la direction opposée à la caméra → face arrière
    return Vector3DotProduct(normal, toCamera) >= 0;
}
#pragma endregion


#pragma region Main
/***********************************************************************************************************/
/********************************************* Main ********************************************************/

int main(void)
{

    // Charger la config au démarrage
    Config cfg = loadConfig("config.ini");

    tilesX = cfg.screen_width / cfg.tile_size;
    tilesY = cfg.screen_height / cfg.tile_size;

    // Utiliser malloc après lecture de la config
    framebuffer = malloc(cfg.screen_width * cfg.screen_height * sizeof(Color));
    if (!framebuffer) {
        printf("ERREUR: malloc framebuffer failed!\n");
        return 1;
    }

    threadData = malloc(cfg.num_threads * sizeof(ThreadData));
    if (!threadData) {
        printf("ERREUR: malloc threadData failed!\n");
        return 1;
    }

    threads = malloc(cfg.num_threads * sizeof(pthread_t));
    if (!threads) {
        printf("ERREUR: malloc threads failed!\n");
        return 1;
    }

    if (cfg.warnock)
        InitWindow(cfg.screen_width, cfg.screen_height, 
            TextFormat("Hybride : Warnock + ZBuffer rendering %dx%d", 
                cfg.screen_width, cfg.screen_height));


    if (cfg.zbuffer)
        InitWindow(cfg.screen_width, cfg.screen_height, 
            TextFormat("ZBuffer rendering %dx%d", 
                cfg.screen_width, cfg.screen_height));

    if (cfg.tiles)
        InitWindow(cfg.screen_width, cfg.screen_height, 
            TextFormat("SoftRender3D - Tiles %dx%d - %d threads", 
                cfg.screen_width, cfg.screen_height, cfg.num_threads));


#pragma region Initialisations
/***********************************************************************************************************/
/******************************************* INIT **********************************************************/

    Camera3D camera = { 0 };
    camera.position = (Vector3){ cfg.cam_x, cfg.cam_y, cfg.cam_z };
    camera.target = (Vector3){ cfg.target_x, cfg.target_y, cfg.target_z };
    camera.up = (Vector3){ 0.0f, -1.0f, 0.0f };
    camera.fovy = cfg.fov;
    camera.projection = CAMERA_PERSPECTIVE;

    Model model = LoadModel(cfg.model_path);

    Mesh mesh = model.meshes[0]; // On prend le premier mesh

    Vector3 *vertices = (Vector3 *)mesh.vertices;
    int vertexCount = mesh.vertexCount;

    float rotX = 0.0f, rotY = 0.0f, rotZ = 0.0f;

    SetTargetFPS(60);

    RenderContext ctx;
    memset(&ctx, 0, sizeof(RenderContext));

    ctx.lightDir = Vector3Normalize((Vector3){ cfg.light_x, cfg.light_y, cfg.light_z });
    ctx.cameraPos = camera.position;
    ctx.screenHeight = cfg.screen_height;
    ctx.screenWidth = cfg.screen_width;
    ctx.tree_depth = cfg.tree_depth;
    ctx.contour_arbre = cfg.contour_arbre;
    ctx.max_poly = cfg.max_poly;
    ctx.ambient = cfg.ambient;
    ctx.diffuse = cfg.diffuse;
    ctx.shininess = cfg.shininess;
    ctx.specular = cfg.specular;
    ctx.refl1 = cfg.refl1;
    ctx.refl2 = cfg.refl2;
    ctx.far = cfg.far;
    ctx.near = cfg.near;
    ctx.fov = cfg.fov;
    ctx.num_threads = cfg.num_threads;
    ctx.tile_size = cfg.tile_size;
    ctx.envMap_enable = cfg.envMap_enable;
    ctx.blur = cfg.blur;
    ctx.radius = cfg.radius;
    ctx.pass = cfg.pass;


    if (cfg.envMap_enable) {
        ctx.envMap = LoadImage(cfg.envMap);
        ImageFormat(&ctx.envMap, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);  // ← forcer RGBA
    } else {
        ctx.envMap.data   = NULL;
        ctx.envMap.width  = 0;
        ctx.envMap.height = 0;
    }

    if (cfg.textures_enabled) {
        ctx.texImage  = LoadImage(cfg.tex_diffuse);
        ImageFormat(&ctx.texImage, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);  // ← forcer RGBA

        ctx.normalMap = LoadImage(cfg.tex_normal);
        ImageFormat(&ctx.normalMap, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    }
    else
    {
        ctx.texImage.data = NULL;
        ctx.normalMap.data = NULL;
    }

    ctx.skyU = malloc(cfg.screen_width * cfg.screen_height * sizeof(float));
    ctx.skyV = malloc(cfg.screen_width * cfg.screen_height * sizeof(float));

    // Tableau de normales par sommet
    Vector3* smoothNormals = calloc(vertexCount, sizeof(Vector3));
    Poly* PolyList = malloc(mesh.vertexCount/3 * sizeof(Poly));
    Poly *visiblePolys = malloc(mesh.triangleCount * sizeof(Poly));
    float* zbuffer;
    Image img;
    Texture2D tex;   

    for (int i = 0; i < mesh.vertexCount / 3; i++) {
        Poly p;
        p.visible = false;
        p.couleur = couleurAleatoire();
        PolyList[i] = p;
    }

    if (cfg.zbuffer)
        zbuffer = malloc(cfg.screen_width * cfg.screen_height * sizeof(float));

    if (cfg.tiles){
        img = GenImageColor(cfg.screen_width, cfg.screen_height, RAYWHITE);
        tex = LoadTextureFromImage(img);
        UnloadImage(img);

        // Init
        pthread_barrier_init(&barrierStart,        NULL, cfg.num_threads + 1);
        pthread_barrier_init(&barrierEnd,          NULL, cfg.num_threads + 1);
        pthread_barrier_init(&barrierSkyboxDone,   NULL, cfg.num_threads + 1);

        initThreads(&ctx);
    }

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
#pragma endregion

#pragma region Boucle principale de rendu
    /***********************************************************************************************************/
    /******************************************* BOUCLE PRINCIPALE DE RENDU ************************************/
    while (!WindowShouldClose())
    {
        //UpdateCamera(&camera, CAMERA_ORBITAL);
        ctx.cameraPos = camera.position;

        rotX += 0.01f;
        rotY -= 0.015f;
        rotZ += 0.015f;

        Matrix rotation = MatrixRotateXYZ((Vector3){ rotX, rotY, rotZ });
        //Matrix rotation = MatrixRotateXYZ((Vector3){ 0, rotY, 0 });

        if (ctx.envMap.data && cfg.tiles){
            
            Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
            Vector3 right   = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
            Vector3 up      = Vector3CrossProduct(right, forward);

            ctx.cameraForward = forward;
            ctx.cameraRight   = right;
            ctx.cameraUp      = up; 

            static Vector3 lastForward = {0};
            if (fabsf(forward.x - lastForward.x) > 0.0001f ||
                fabsf(forward.y - lastForward.y) > 0.0001f ||
                fabsf(forward.z - lastForward.z) > 0.0001f)
            {
                PrecomputeSkyboxLUT(&ctx);
                lastForward = forward;
            }
        }

        BeginDrawing();
        ClearBackground(BLACK);

        // Frustum culling
        Matrix proj    = buildProjectionMatrix(&ctx);
        Matrix view    = GetCameraMatrix(camera);
        Matrix vp      = MatrixMultiply(view, proj);  // ← view PUIS proj
        Frustum frustum = extractFrustum(vp);
        
#pragma region BOUCLE qui Prépare les polygones : transforme, filtre (backface/frustum) et projette chaque triangle du mesh en Poly prêt à rasterizer.
        int inc = 0;
        for (int i = 0; i < mesh.triangleCount; i++)
        {
            int i0 = i*3, i1 = i*3+1, i2 = i*3+2;

            // Vertices transformés (une seule fois)
            Vector3 v0 = Vector3Transform(getVertex(mesh, i0), rotation);
            Vector3 v1 = Vector3Transform(getVertex(mesh, i1), rotation);
            Vector3 v2 = Vector3Transform(getVertex(mesh, i2), rotation);

            // Backface culling
            if (cfg.backface_culling && isBackFace(v0, v1, v2, ctx.cameraPos))
                continue;

            // Frustum culling
            if (cfg.frustum_culling) {
                Vector3 aabbMin = { fminf(v0.x,fminf(v1.x,v2.x)), fminf(v0.y,fminf(v1.y,v2.y)), fminf(v0.z,fminf(v1.z,v2.z)) };
                Vector3 aabbMax = { fmaxf(v0.x,fmaxf(v1.x,v2.x)), fmaxf(v0.y,fmaxf(v1.y,v2.y)), fmaxf(v0.z,fmaxf(v1.z,v2.z)) };
                if (!aabbInFrustum(&frustum, aabbMin, aabbMax)) continue;
                if (!triangleInFrustum(&frustum, v0, v1, v2))  continue;
            }

            Poly* p = &visiblePolys[inc];

            // Projection
            p->p0 = GetWorldToScreen(v0, camera);
            p->p1 = GetWorldToScreen(v1, camera);
            p->p2 = GetWorldToScreen(v2, camera);

            // Z (espace caméra)
            Vector3 cv0 = Vector3Transform(v0, view);
            Vector3 cv1 = Vector3Transform(v1, view);
            Vector3 cv2 = Vector3Transform(v2, view);
            p->z0 = cv0.z; p->z1 = cv1.z; p->z2 = cv2.z;
            p->zmin = fminf(p->z0, fminf(p->z1, p->z2));
            p->zmax = fmaxf(p->z0, fmaxf(p->z1, p->z2));

            // Bounding box écran
            float minX = fminf(p->p0.x, fminf(p->p1.x, p->p2.x));
            float maxX = fmaxf(p->p0.x, fmaxf(p->p1.x, p->p2.x));
            float minY = fminf(p->p0.y, fminf(p->p1.y, p->p2.y));
            float maxY = fmaxf(p->p0.y, fmaxf(p->p1.y, p->p2.y));

            p->minX = fmaxf(0.0f, minX);
            p->maxX = fminf(ctx.screenWidth-1,  maxX);
            p->minY = fmaxf(0.0f, minY);
            p->maxY = fminf(ctx.screenHeight-1, maxY);

            if (p->maxX < p->minX || p->maxY < p->minY) continue;

            p->tileMinX = (int)(p->minX) / ctx.tile_size;
            p->tileMaxX = (int)(p->maxX) / ctx.tile_size;
            p->tileMinY = (int)(p->minY) / ctx.tile_size;
            p->tileMaxY = (int)(p->maxY) / ctx.tile_size;

            // UVs
            p->uv0 = getUV(mesh, i0);
            p->uv1 = getUV(mesh, i1);
            p->uv2 = getUV(mesh, i2);

            // Vertices world space (pour lighting)
            p->v0 = v0; p->v1 = v1; p->v2 = v2;

            // Tangent/Bitangent (maintenant v0/v1/v2 sont bien assignés)
            Vector3 edge1 = Vector3Subtract(v1, v0);
            Vector3 edge2 = Vector3Subtract(v2, v0);
            float duv1x = p->uv1.x - p->uv0.x, duv1y = p->uv1.y - p->uv0.y;
            float duv2x = p->uv2.x - p->uv0.x, duv2y = p->uv2.y - p->uv0.y;
            float denom = duv1x * duv2y - duv2x * duv1y;

            if (fabsf(denom) < 1e-6f) {
                p->tangent   = (Vector3){1,0,0};
                p->bitangent = (Vector3){0,1,0};
            } else {
                float f = 1.0f / denom;
                p->tangent   = Vector3Normalize((Vector3){
                    f*(duv2y*edge1.x - duv1y*edge2.x),
                    f*(duv2y*edge1.y - duv1y*edge2.y),
                    f*(duv2y*edge1.z - duv1y*edge2.z)});
                p->bitangent = Vector3Normalize((Vector3){
                    f*(-duv2x*edge1.x + duv1x*edge2.x),
                    f*(-duv2x*edge1.y + duv1x*edge2.y),
                    f*(-duv2x*edge1.z + duv1x*edge2.z)});
            }

            // Normales
            p->n0 = Vector3Normalize(Vector3Transform(getNormal(mesh, i0, smoothNormals), rotation));
            p->n1 = Vector3Normalize(Vector3Transform(getNormal(mesh, i1, smoothNormals), rotation));
            p->n2 = Vector3Normalize(Vector3Transform(getNormal(mesh, i2, smoothNormals), rotation));

            p->tangent   = Vector3Normalize(Vector3Transform(p->tangent,   rotation));
            p->bitangent = Vector3Normalize(Vector3Transform(p->bitangent, rotation));

            p->couleur = PolyList[i].couleur;
            if (cfg.zbuffer)
                gouraudShading(&ctx, p);
            if (cfg.warnock)
                flatShading(&ctx, p, view);

            p->visible = true;

            inc++;
        }

        int polyCount = inc;
        ctx.polys = visiblePolys;
        ctx.polyCount = polyCount;
#pragma endregion

        static int displayTotal   = 0;
        static int displayRendus  = 0;
        static int displayRejetes = 0;
        static int frameCounter   = 0;

        frameCounter++;
        if (frameCounter % 2 == 0)  // mise à jour toutes les 2 frames
        {
            displayTotal   = mesh.triangleCount;
            displayRendus  = polyCount;
            displayRejetes = mesh.triangleCount - polyCount;
        }

        if (IsKeyDown(KEY_UP))    camera.position.y += 1.0f;
        if (IsKeyDown(KEY_DOWN))  camera.position.y -= 1.0f;
        if (IsKeyDown(KEY_RIGHT)) camera.position.x += 1.0f;
        if (IsKeyDown(KEY_LEFT))  camera.position.x -= 1.0f;
        if (IsKeyDown(KEY_W))     camera.position.z -= 1.0f;
        if (IsKeyDown(KEY_S))     camera.position.z += 1.0f;

        if (IsKeyDown(KEY_V))     camera.target.y += 1.0f;
        if (IsKeyDown(KEY_T))     camera.target.y -= 1.0f;
        if (IsKeyDown(KEY_F))     camera.target.x += 1.0f;
        if (IsKeyDown(KEY_H))     camera.target.x -= 1.0f;
        if (IsKeyDown(KEY_P))     camera.target.z += 1.0f;
        if (IsKeyDown(KEY_L))     camera.target.z -= 1.0f;

        if (cfg.warnock){
            DrawText(TextFormat("Hybride : Warnock + ZBuffer, Profondeur de l'arbre = %d", cfg.tree_depth), 10, 10, 20, WHITE);

            Region root = {0,0,cfg.screen_width,cfg.screen_height};
            int indices[cfg.max_poly];
            for (int i = 0; i < polyCount; i++)
                indices[i] = i;

            ctx.rootIndices = indices;
            warnock(&ctx, &root, indices, polyCount, 0);
        }

        if (cfg.zbuffer){
            DrawText("ZBuffer", 10, 10, 20, WHITE);

            for (int i = 0; i < cfg.screen_width * cfg.screen_height; i++)
                zbuffer[i] = 1e9; // très loin

            for (int i = 0; i < polyCount; i++)
            {
                //drawTriangleZ(&visiblePolys[i], zbuffer);
                drawTriangleZ(&ctx, &ctx.polys[i], zbuffer);
            }
        }

        if (cfg.tiles){
            qsort(ctx.polys, ctx.polyCount, sizeof(Poly), compare_zmin);

            for (int i = 0; i < tilesX * tilesY; i++){
                tiles[i].minZ = 1e9f;
                tiles[i].maxZ = -1e9f;
            }

            binTriangles(&ctx);

            renderFrame(&ctx);
            UpdateTexture(tex, framebuffer);
            DrawTexture(tex, 0, 0, WHITE);

            if (cfg.debug_tiles){
                /*** DEBUG TILES ***/
                /*
                // debug : tiles avec triangles
                for (int ty = 0; ty < tilesY; ty++)
                {
                    for (int tx = 0; tx < tilesX; tx++)
                    {
                        Tile* tile = &tiles[ty * tilesX + tx];

                        if (tile->count == 0) continue;

                        int x = tx * cfg.tile_size;
                        int y = ty * cfg.tile_size;

                        DrawRectangleLines(x, ctx.screenHeight - (y + cfg.tile_size), cfg.tile_size, cfg.tile_size, GREEN);
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

                        int x = tx * cfg.tile_size;
                        int y = ty * cfg.tile_size;

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

                        DrawRectangleLinesEx((Rectangle){x, cfg.screen_height - (y + cfg.tile_size), cfg.tile_size, cfg.tile_size},1.5f, col);
                        DrawText(TextFormat("%d", tile->count), x+2, cfg.screen_height - y - 14, 10, whiteText);
                    }
                }

                //debugDrawTileCoverage(&ctx);
            }

        DrawText("Tiles", 10, 10, 20, WHITE);
        }

        DrawText(TextFormat("FPS = %d", GetFPS()),
         10, 40, 20, WHITE);

        DrawText(TextFormat("Triangles total  : %d", displayTotal), 10, 70, 20, WHITE);
        DrawText(TextFormat("Triangles rendus : %d", displayRendus),  10, 95, 20, WHITE);
        DrawText(TextFormat("Triangles rejetés: %d", displayRejetes), 10, 120, 20, WHITE);

        // Afficher la position pour la noter
        DrawText(TextFormat("pos: %.1f %.1f %.1f", 
            camera.position.x, camera.position.y, camera.position.z), 
            10, 150, 20, WHITE);

        // Afficher la target pour la noter
        DrawText(TextFormat("Target: %.1f %.1f %.1f", 
            camera.target.x, camera.target.y, camera.target.z), 
            10, 180, 20, WHITE);

        EndDrawing();
    }
#pragma endregion

    free(threads);
    free(threadData);
    free(framebuffer);
    free(smoothNormals);
    free(PolyList);
    free(visiblePolys);

    if (cfg.zbuffer)
        free(zbuffer);

    if (ctx.skyU) free(ctx.skyU);
    if (ctx.skyV) free(ctx.skyV);

    UnloadModel(model);
    if (ctx.texImage.data)  UnloadImage(ctx.texImage);
    if (ctx.normalMap.data) UnloadImage(ctx.normalMap);
    if (ctx.envMap.data) UnloadImage(ctx.envMap);

    CloseWindow();
    return 0;
}
#pragma endregion