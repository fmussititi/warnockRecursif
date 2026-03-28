#include "utils.h"
#include "globals.h"
#include <math.h>
#include <stdlib.h>

Color couleurAleatoire(void) {
    Color couleurs[5] = { RED, GREEN, BLUE, YELLOW, ORANGE };
    return couleurs[GetRandomValue(0, 4)];
}

Vector3 getNormal(Mesh mesh, int index, Vector3* smoothNormals) {
    return smoothNormals[index];
}

Vector3 getVertex(Mesh mesh, int index) {
    return (Vector3){
        mesh.vertices[index*3 + 0],
        mesh.vertices[index*3 + 1],
        mesh.vertices[index*3 + 2]
    };
}

Vector2 getUV(Mesh mesh, int index) {
    if (!mesh.texcoords) return (Vector2){0, 0};
    return (Vector2){
        mesh.texcoords[index*2 + 0],
        mesh.texcoords[index*2 + 1]
    };
}

int compare_zmin(const void* a, const void* b) {
    Poly* p1 = (Poly*)a;
    Poly* p2 = (Poly*)b;
    if (p1->zmin < p2->zmin) return -1;
    if (p1->zmin > p2->zmin) return  1;
    return 0;
}

// Dans utils.h — comparateur inverse (du plus loin au plus proche)
int compare_zmax_desc(const void* a, const void* b) {
    Poly* p1 = (Poly*)a;
    Poly* p2 = (Poly*)b;
    if (p1->zmin > p2->zmin) return -1;
    if (p1->zmin < p2->zmin) return  1;
    return 0;
}

// Clear framebuffer
void clear_framebuffer(RenderContext* ctx, Color clearColor) {
    //Color clearColor = (Color){ 20, 20, 30, 255 };
    int total = ctx->screenWidth * ctx->screenHeight;
    for (int i = 0; i < total; i++)
        framebuffer[i] = clearColor;
}

void DrawRectangleFramebuffer(RenderContext* ctx, int left, int top, int width, int height, Color color)
{
    // Clamp aux bords de l'écran
    int x0 = left;
    int y0 = top;
    int x1 = left + width;
    int y1 = top  + height;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > ctx->screenWidth)  x1 = ctx->screenWidth;
    if (y1 > ctx->screenHeight) y1 = ctx->screenHeight;

    for (int y = y0; y < y1; y++) {
        Color* row = &framebuffer[y * ctx->screenWidth + x0];
        int len = x1 - x0;
        // Remplir la première case puis dupliquer — plus rapide qu'une boucle pixel
        for (int x = 0; x < len; x++)
            row[x] = color;
    }
}

void DrawRectangleLinesFramebuffer(RenderContext* ctx, int left, int top, int width, int height, Color color)
{
    int x0 = left;
    int y0 = top;
    int x1 = left + width  - 1;
    int y1 = top  + height - 1;

    // Clamp
    int cx0 = (x0 < 0) ? 0 : x0;
    int cy0 = (y0 < 0) ? 0 : y0;
    int cx1 = (x1 >= ctx->screenWidth)  ? ctx->screenWidth  - 1 : x1;
    int cy1 = (y1 >= ctx->screenHeight) ? ctx->screenHeight - 1 : y1;

    // Ligne du haut
    if (y0 >= 0 && y0 < ctx->screenHeight)
        for (int x = cx0; x <= cx1; x++)
            framebuffer[y0 * ctx->screenWidth + x] = color;

    // Ligne du bas
    if (y1 >= 0 && y1 < ctx->screenHeight)
        for (int x = cx0; x <= cx1; x++)
            framebuffer[y1 * ctx->screenWidth + x] = color;

    // Ligne gauche
    if (x0 >= 0 && x0 < ctx->screenWidth)
        for (int y = cy0; y <= cy1; y++)
            framebuffer[y * ctx->screenWidth + x0] = color;

    // Ligne droite
    if (x1 >= 0 && x1 < ctx->screenWidth)
        for (int y = cy0; y <= cy1; y++)
            framebuffer[y * ctx->screenWidth + x1] = color;
}

void DrawTriangleFramebuffer(RenderContext* ctx, Poly* tri, Color color)
{
    int minX = (int)fmaxf(0,                  floorf(tri->minX));
    int maxX = (int)fminf(ctx->screenWidth-1,  ceilf(tri->maxX));
    int minY = (int)fmaxf(0,                  floorf(tri->minY));
    int maxY = (int)fminf(ctx->screenHeight-1, ceilf(tri->maxY));

    if (minX > maxX || minY > maxY) return;

    // Edge functions
    float area    = (tri->p1.x-tri->p0.x)*(tri->p2.y-tri->p0.y) -
                    (tri->p2.x-tri->p0.x)*(tri->p1.y-tri->p0.y);
    if (fabsf(area) < 1e-6f) return;
    float invArea = 1.0f / area;

    float e0A = tri->p0.y - tri->p1.y, e0B = tri->p1.x - tri->p0.x;
    float e1A = tri->p1.y - tri->p2.y, e1B = tri->p2.x - tri->p1.x;
    float e2A = tri->p2.y - tri->p0.y, e2B = tri->p0.x - tri->p2.x;

    float w0_row = e0A*(minX+0.5f) + e0B*(minY+0.5f) + tri->p0.x*tri->p1.y - tri->p0.y*tri->p1.x;
    float w1_row = e1A*(minX+0.5f) + e1B*(minY+0.5f) + tri->p1.x*tri->p2.y - tri->p1.y*tri->p2.x;
    float w2_row = e2A*(minX+0.5f) + e2B*(minY+0.5f) + tri->p2.x*tri->p0.y - tri->p2.y*tri->p0.x;

    for (int y = minY; y <= maxY; y++) {
        float w0 = w0_row, w1 = w1_row, w2 = w2_row;

        for (int x = minX; x <= maxX; x++) {
            if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
                int fbY = ctx->screenHeight - y;
                if (fbY >= 0 && fbY < ctx->screenHeight)
                    framebuffer[fbY * ctx->screenWidth + x] = color;
            }
            w0 += e0A; w1 += e1A; w2 += e2A;
        }
        w0_row += e0B; w1_row += e1B; w2_row += e2B;
    }
}

// Voici une version simplifiée de ce que tu dois faire :
// 1. Parcourir tous tes 17406 sommets.
// 2. Pour chaque sommet, vérifier s'il existe déjà dans un nouveau tableau "uniques".
// 3. Si oui, on ajoute la normale du sommet actuel à la normale du sommet unique trouvé.
// 4. Si non, on l'ajoute comme nouveau sommet unique.

// Raylib propose une fonction (souvent dans les extras ou raymath) 
// Mais voici comment le faire proprement pour ton moteur :

Mesh OptimizeMesh(Mesh mesh) {
    Mesh optimized = { 0 };
    
    Vector3 *uniquePositions = malloc(mesh.vertexCount * sizeof(Vector3));
    Vector3 *accumulatedNormals = calloc(mesh.vertexCount, sizeof(Vector3));
    // Ajoutons les UVs pour éviter que le modèle disparaisse
    Vector2 *uniqueTexCoords = malloc(mesh.vertexCount * sizeof(Vector2));
    
    int *indicesMap = malloc(mesh.vertexCount * sizeof(int));
    int uniqueCount = 0;

    for (int i = 0; i < mesh.vertexCount; i++) {
        // Extraction sécurisée des données de l'ancien mesh
        Vector3 v = { mesh.vertices[i*3], mesh.vertices[i*3+1], mesh.vertices[i*3+2] };
        Vector3 n = { mesh.normals[i*3], mesh.normals[i*3+1], mesh.normals[i*3+2] };
        Vector2 uv = { 0 };
        if (mesh.texcoords) {
            uv.x = mesh.texcoords[i*2];
            uv.y = mesh.texcoords[i*2+1];
        }

        int foundIndex = -1;
        for (int j = 0; j < uniqueCount; j++) {
            // On compare la position ET l'UV (optionnel mais conseillé pour les textures)
            if (Vector3Distance(v, uniquePositions[j]) < 0.0001f &&
                fabsf(uv.x - uniqueTexCoords[j].x) < 0.0001f &&
                fabsf(uv.y - uniqueTexCoords[j].y) < 0.0001f) {
                foundIndex = j;
                break;
            }
        }

        if (foundIndex >= 0) {
            accumulatedNormals[foundIndex] = Vector3Add(accumulatedNormals[foundIndex], n);
            indicesMap[i] = foundIndex;
        } else {
            uniquePositions[uniqueCount] = v;
            accumulatedNormals[uniqueCount] = n;
            if (mesh.texcoords) uniqueTexCoords[uniqueCount] = uv;
            indicesMap[i] = uniqueCount;
            uniqueCount++;
        }
    }

    // Normalisation
    for (int i = 0; i < uniqueCount; i++) accumulatedNormals[i] = Vector3Normalize(accumulatedNormals[i]);

    // Allocalion du nouveau Mesh
    optimized.vertexCount = uniqueCount;
    optimized.triangleCount = mesh.vertexCount / 3;
    optimized.vertices = (float *)malloc(uniqueCount * 3 * sizeof(float));
    optimized.normals = (float *)malloc(uniqueCount * 3 * sizeof(float));
    optimized.indices = (unsigned short *)malloc(mesh.vertexCount * sizeof(unsigned short));
    if (mesh.texcoords) optimized.texcoords = (float *)malloc(uniqueCount * 2 * sizeof(float));

    // Remplissage
    for (int i = 0; i < uniqueCount; i++) {
        optimized.vertices[i*3+0] = uniquePositions[i].x;
        optimized.vertices[i*3+1] = uniquePositions[i].y;
        optimized.vertices[i*3+2] = uniquePositions[i].z;
        optimized.normals[i*3+0] = accumulatedNormals[i].x;
        optimized.normals[i*3+1] = accumulatedNormals[i].y;
        optimized.normals[i*3+2] = accumulatedNormals[i].z;
        if (mesh.texcoords) {
            optimized.texcoords[i*2+0] = uniqueTexCoords[i].x;
            optimized.texcoords[i*2+1] = uniqueTexCoords[i].y;
        }
    }

    for (int i = 0; i < mesh.vertexCount; i++) {
        ((unsigned short *)optimized.indices)[i] = (unsigned short)indicesMap[i];
    }

    free(uniquePositions);
    free(accumulatedNormals);
    free(uniqueTexCoords);
    free(indicesMap);

    // CRUCIAL pour Raylib
    UploadMesh(&optimized, false); 
    
    return optimized;
}
