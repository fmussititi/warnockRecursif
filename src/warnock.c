#include "warnock.h"
#include "utils.h"
#include "globals.h"
#include <math.h>
#include <stdbool.h>

void subdivise(Region* R, Region regions[4]) {
    regions[0] = (Region){R->x1,             (R->y1+R->y2)/2, (R->x1+R->x2)/2, R->y2};
    regions[1] = (Region){(R->x1+R->x2)/2,   (R->y1+R->y2)/2, R->x2,           R->y2};
    regions[2] = (Region){R->x1,             R->y1,           (R->x1+R->x2)/2, (R->y1+R->y2)/2};
    regions[3] = (Region){(R->x1+R->x2)/2,   R->y1,           R->x2,           (R->y1+R->y2)/2};
}

// Fonction utilitaire pour tester si un rectangle est du côté "extérieur" d'une arête
// On prend le coin du rectangle qui maximise la fonction de distance signée.
static inline bool IsRegionOutsideEdge(Region* R, EdgeLine* e) {
    float val = e->C;
    // On choisit le coin (x,y) qui donne la valeur maximale pour Ax + By + C
    val += (e->A > 0) ? e->A * R->x2 : e->A * R->x1;
    val += (e->B > 0) ? e->B * R->y2 : e->B * R->y1;
    
    // Si même le point le plus "favorable" est < 0, le rectangle est hors du demi-plan
    return val < 0; 
}

bool TriangleIntersectsRegion(Region* R, Poly* tri) {
    // 1. Test AABB (Largeur/Hauteur) - Le plus rapide
    if (tri->maxX < R->x1 || tri->minX > R->x2 ||
        tri->maxY < R->y1 || tri->minY > R->y2) return false;

    for (int i = 0; i < 3; i++) {

        // 3. Si la région est totalement à l'extérieur d'une seule arête, 
        // alors il n'y a pas d'intersection (Théorème de l'axe séparateur).
        if (IsRegionOutsideEdge(R, &tri->lines[i])) return false;
    }

    // 4. Si on passe les tests ci-dessus, il y a soit intersection, 
    // soit le rectangle est entièrement DANS le triangle.
    return true;
}

bool AABBOverlap(Region* R, Poly* p) {
    return !(p->maxX < R->x1 || p->minX > R->x2 ||
             p->maxY < R->y1 || p->minY > R->y2);
}

int region_fully_covered(Region* R, Poly* tri) {
// On teste les 3 arêtes du triangle
    for (int i = 0; i < 3; i++) {
        EdgeLine* e = &tri->lines[i];
        
        // Pour que la région soit entièrement couverte, il faut que le coin
        // le PLUS PROCHE de l'extérieur soit quand même à l'intérieur.
        // On cherche donc le coin (x,y) qui MINIMISE Ax + By + C.
        float min_val = e->C;
        min_val += (e->A > 0) ? e->A * R->x1 : e->A * R->x2;
        min_val += (e->B > 0) ? e->B * R->y1 : e->B * R->y2;

        // Si le pire coin est < 0, alors au moins une partie du rectangle
        // est en dehors de cette arête -> Pas de couverture totale.
        if (min_val < 0) return 0;
    }

    return 1;
}

void drawRegionZBuffer(RenderContext* ctx, Region* R, Poly* polys, int* indices, int count)
{
    int width  = R->x2 - R->x1;
    int height = R->y2 - R->y1;

    float zbuf[width * height];
    for (int i = 0; i < width * height; i++)
        zbuf[i] = 1e9f;

    for (int i = 0; i < count; i++) {
        Poly* tri = &polys[indices[i]];

        int minX = (int)fmaxf(R->x1, floorf(tri->minX));
        int maxX = (int)fminf(R->x2-1, ceilf(tri->maxX));
        int minY = (int)fmaxf(R->y1, floorf(tri->minY));
        int maxY = (int)fminf(R->y2-1, ceilf(tri->maxY));

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

        //for (int y = minY; y <= maxY; y += height) {
        for (int y = minY; y <= maxY; y++) {
            float w0 = w0_row, w1 = w1_row, w2 = w2_row;
            //for (int x = minX; x <= maxX; x += width) {
            for (int x = minX; x <= maxX; x++) {
                if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
                    float alpha = w1 * invArea;
                    float beta  = w2 * invArea;
                    float gamma = 1.0f - alpha - beta;
                    float z     = alpha*tri->z0 + beta*tri->z1 + gamma*tri->z2;

                    int lx    = x - R->x1;
                    int ly    = y - R->y1;
                    int index = ly * width + lx;

                    if (z < zbuf[index]) {
                        zbuf[index] = z;
                        //DrawRectangle(x, ctx->screenHeight - y, width+1, height+1, tri->couleur);
                        //DrawRectangleFramebuffer(ctx, x, ctx->screenHeight - y, width, height, tri->couleur);
                        int fbY = ctx->screenHeight - y;
                        if (fbY >= 0 && fbY < ctx->screenHeight)
                            framebuffer[fbY * ctx->screenWidth + x] = tri->couleur; 
                    }
                }
                w0 += e0.A; w1 += e1.A; w2 += e2.A;
            }
            w0_row += e0.B; w1_row += e1.B; w2_row += e2.B;
        }
    }
}

static inline float GetZAt(Poly* tri, float x, float y) {
    // Si le triangle est vertical (C proche de 0), on retourne une valeur par défaut
    if (fabsf(tri->plane.C) < 1e-6f) return tri->zmin;
    
    // z = -(Ax + By + D) / C
    return -(tri->plane.A * x + tri->plane.B * y + tri->plane.D) / tri->plane.C;
}

static bool isADevantB(Region* R, Poly* A, Poly* B) {
    // Test rapide : si la boîte Z de A est devant B, pas besoin de calculs de plans
    if (A->zmax < B->zmin) return true;
    // Si la boîte Z de B est devant A, A ne peut pas être devant
    if (B->zmax < A->zmin) return false;

    // Test des 4 coins de la région
    float cornersX[4] = {(float)R->x1, (float)R->x2, (float)R->x1, (float)R->x2};
    float cornersY[4] = {(float)R->y1, (float)R->y1, (float)R->y2, (float)R->y2};

    for (int i = 0; i < 4; i++) {
        float zA = GetZAt(A, cornersX[i], cornersY[i]);
        float zB = GetZAt(B, cornersX[i], cornersY[i]);

        // Si à n'importe quel coin le Z de A est derrière celui de B, 
        // A n'est pas "FrontMost" (ou ils s'intersectent).
        if (zA > zB) return false;
    }

    return true;
}


static int isFrontMost(Region* R, Poly* A, Poly* polys, int* indices, int count)
{
    for (int i = 0; i < count; i++) {
        Poly* B = &polys[indices[i]];

        if (B == A) continue;

        // Si A n'est pas devant B sur toute la surface du rectangle R,
        // on renvoie 0 (ce qui forcera Warnock à subdiviser)
        if (!isADevantB(R, A, B)) {
            return 0;
        }
    }
    return 1;
}

void DrawTexturedRegion(RenderContext* ctx, Region* R, Poly* tri) {
    const Color* texPixels = (Color*)ctx->texImage.data;
    int texW = ctx->texImage.width;
    int texH = ctx->texImage.height;

    for (int y = R->y1; y < R->y2; y++) {
        int fbY = ctx->screenHeight - y;
        if (fbY < 0 || fbY >= ctx->screenHeight) continue;

        float currentU = tri->eqU.a * (R->x1 + 0.5f) + tri->eqU.b * (y + 0.5f) + tri->eqU.c;
        float currentV = tri->eqV.a * (R->x1 + 0.5f) + tri->eqV.b * (y + 0.5f) + tri->eqV.c;

        for (int x = R->x1; x < R->x2; x++) {
            // 1. Gestion du wrapping (répétition de la texture)
            // On utilise floorf pour gérer correctement les nombres négatifs
            int tx = (int)((currentU - floorf(currentU)) * texW);
            int ty = (int)((currentV - floorf(currentV)) * texH);

            // 2. Sécurité ultime (Clamping) pour éviter le pixel exact sur la bordure droite/basse
            if (tx < 0) tx = 0; if (tx >= texW) tx = texW - 1;
            if (ty < 0) ty = 0; if (ty >= texH) ty = texH - 1;

            Color texCol = texPixels[ty * texW + tx];

            // --- APPLICATION DU FLAT SHADING ---
            // On module la couleur de la texture par l'intensité
            texCol.r = (unsigned char)(texCol.r * tri->intensity);
            texCol.g = (unsigned char)(texCol.g * tri->intensity);
            texCol.b = (unsigned char)(texCol.b * tri->intensity);
            
            // On vérifie que le pixel écran est bien dans le framebuffer
            if (x >= 0 && x < ctx->screenWidth) {
                framebuffer[fbY * ctx->screenWidth + x] = texCol;
            }

            currentU += tri->eqU.a;
            currentV += tri->eqV.a;
        }
    }
}

void DrawNormalMappedRegion(RenderContext* ctx, Region* R, Poly* tri) {
    const Color* texPixels = (Color*)ctx->texImage.data;
    const Color* normPixels = (Color*)ctx->normalMap.data; 

    for (int y = R->y1; y < R->y2; y++) {
        int fbY = ctx->screenHeight - y;
        if (fbY < 0 || fbY >= ctx->screenHeight) continue;

        // Init interpolation pour la ligne
        float curNx = tri->eqNx.a * (R->x1 + 0.5f) + tri->eqNx.b * (y + 0.5f) + tri->eqNx.c;
        float curNy = tri->eqNy.a * (R->x1 + 0.5f) + tri->eqNy.b * (y + 0.5f) + tri->eqNy.c;
        float curNz = tri->eqNz.a * (R->x1 + 0.5f) + tri->eqNz.b * (y + 0.5f) + tri->eqNz.c;
        // ... tes currentU, currentV ...
        float currentU = tri->eqU.a * (R->x1 + 0.5f) + tri->eqU.b * (y + 0.5f) + tri->eqU.c;
        float currentV = tri->eqV.a * (R->x1 + 0.5f) + tri->eqV.b * (y + 0.5f) + tri->eqV.c;

        for (int x = R->x1; x < R->x2; x++) {
            // 1. Normale lissée (Interpolée)
            Vector3 interpolatedN = { curNx, curNy, curNz };
            // On la normalise pour corriger l'erreur d'interpolation linéaire
            interpolatedN = Vector3Normalize(interpolatedN);

            // 2. TBN Matrix (On utilise la normale lissée au lieu de celle du plan)
            // On peut aussi recalculer B = Cross(N, T) pour plus de précision
            Vector3 T = tri->tangent;
            Vector3 B = tri->bitangent;

            // 3. Sampling Normal Map
            int ntx = (int)((currentU - floorf(currentU)) * ctx->normalMap.width);
            int nty = (int)((currentV - floorf(currentV)) * ctx->normalMap.height);
            Color nCol = normPixels[nty * ctx->normalMap.width + ntx];
            Vector3 tangentN = {
                (nCol.r / 255.0f) * 2.0f - 1.0f,
                (nCol.g / 255.0f) * 2.0f - 1.0f,
                (nCol.b / 255.0f) * 2.0f - 1.0f
            };

            // 4. Combinaison finale de la normale
            Vector3 pixelNormal = {
                T.x * tangentN.x + B.x * tangentN.y + interpolatedN.x * tangentN.z,
                T.y * tangentN.x + B.y * tangentN.y + interpolatedN.y * tangentN.z,
                T.z * tangentN.x + B.z * tangentN.y + interpolatedN.z * tangentN.z
            };

            // 5. Light
            float dot = Vector3DotProduct(Vector3Normalize(pixelNormal), ctx->lightDir);
            float intensity = fmaxf(ctx->ambient, dot);

            int dtx = (int)((currentU - floorf(currentU)) * ctx->texImage.width);
            int dty = (int)((currentV - floorf(currentV)) * ctx->texImage.height);
            // ... Application couleur ...
            Color texCol = texPixels[dty * ctx->texImage.width + dtx];
            texCol.r = (unsigned char)(texCol.r * intensity);
            texCol.g = (unsigned char)(texCol.g * intensity);
            texCol.b = (unsigned char)(texCol.b * intensity);

            if (x >= 0 && x < ctx->screenWidth)
                framebuffer[fbY * ctx->screenWidth + x] = texCol;

            // Incréments
            curNx += tri->eqNx.a; curNy += tri->eqNy.a; curNz += tri->eqNz.a;
            currentU += tri->eqU.a; currentV += tri->eqV.a;
        }
    }
}

void DrawFullShaderRegion(RenderContext* ctx, Region* R, Poly* tri)
{
    const Color* texPixels  = (Color*)ctx->texImage.data;
    const Color* normPixels = (Color*)ctx->normalMap.data;

    // Lumière en VIEW SPACE (IMPORTANT)
    Vector3 lightDir = Vector3Normalize(ctx->lightDir);

    for (int y = R->y1; y < R->y2; y++)
    {
        int fbY = ctx->screenHeight - y;
        if (fbY < 0 || fbY >= ctx->screenHeight) continue;

        float px = R->x1 + 0.5f;
        float py = y + 0.5f;

        // Interpolation initiale
        float curPx = tri->eqPx.a * px + tri->eqPx.b * py + tri->eqPx.c;
        float curPy = tri->eqPy.a * px + tri->eqPy.b * py + tri->eqPy.c;
        float curPz = tri->eqPz.a * px + tri->eqPz.b * py + tri->eqPz.c;

        float curNx = tri->eqNx.a * px + tri->eqNx.b * py + tri->eqNx.c;
        float curNy = tri->eqNy.a * px + tri->eqNy.b * py + tri->eqNy.c;
        float curNz = tri->eqNz.a * px + tri->eqNz.b * py + tri->eqNz.c;

        float currentU = tri->eqU.a * px + tri->eqU.b * py + tri->eqU.c;
        float currentV = tri->eqV.a * px + tri->eqV.b * py + tri->eqV.c;

        for (int x = R->x1; x < R->x2; x++)
        {
            // =======================
            // VIEW DIR (VIEW SPACE)
            // =======================
            Vector3 pixelPos = { curPx, curPy, curPz };
            Vector3 viewDir  = Vector3Normalize(Vector3Negate(pixelPos));

            // =======================
            // NORMAL INTERPOLÉE
            // =======================
            Vector3 N = Vector3Normalize((Vector3){ curNx, curNy, curNz });

            // =======================
            // UV SAFE WRAP
            // =======================
            int ntx = abs((int)(currentU * ctx->normalMap.width)) % ctx->normalMap.width;
            int nty = abs((int)(currentV * ctx->normalMap.height)) % ctx->normalMap.height;

            Color nCol = normPixels[nty * ctx->normalMap.width + ntx];

            // normal map -> [-1,1]
            Vector3 tangentN = {
                (nCol.r / 255.0f) * 2.0f - 1.0f,
                (nCol.g / 255.0f) * 2.0f - 1.0f,
                (nCol.b / 255.0f) * 2.0f - 1.0f
            };

            // =======================
            // NORMAL STRENGTH (optionnel mais important)
            // =======================
            float normalStrength = 0.6f;
            tangentN.x *= normalStrength;
            tangentN.y *= normalStrength;
            tangentN.z = (1.0f - normalStrength) + tangentN.z * normalStrength;

            // =======================
            // TBN (simplifié et stable)
            // =======================
            Vector3 T = Vector3Normalize(tri->tangent);
            Vector3 B = Vector3Normalize(tri->bitangent);

            Vector3 finalN = {
                T.x * tangentN.x + B.x * tangentN.y + N.x * tangentN.z,
                T.y * tangentN.x + B.y * tangentN.y + N.y * tangentN.z,
                T.z * tangentN.x + B.z * tangentN.y + N.z * tangentN.z
            };
            finalN = Vector3Normalize(Vector3Negate(finalN));

            // =======================
            // LIGHTING
            // =======================
            float dotNL = fmaxf(Vector3DotProduct(finalN, lightDir), 0.0f);

            float diffuse = dotNL;

            Vector3 halfDir = Vector3Normalize(Vector3Add(lightDir, viewDir));
            float dotNH = fmaxf(Vector3DotProduct(finalN, halfDir), 0.0f);
            //float spec = powf(dotNH, ctx->shininess);
            // --- Remplacement de powf ---
            int lutIndex = (int)(dotNH * (SPEC_LUT_SIZE - 1));
            float spec = specLUT[lutIndex]; 
            // ----------------------------

            float lighting = fminf(ctx->ambient + diffuse * ctx->diffuse, 1.0f);
            float specular = spec * ctx->specular;

            // =======================
            // TEXTURE
            // =======================
            int dtx = abs((int)(currentU * ctx->texImage.width)) % ctx->texImage.width;
            int dty = abs((int)(currentV * ctx->texImage.height)) % ctx->texImage.height;

            Color tex = texPixels[dty * ctx->texImage.width + dtx];

            // =======================
            // FINAL COLOR
            // =======================
            float r = tex.r * lighting + tex.r * specular;
            float g = tex.g * lighting + tex.g * specular;
            float b = tex.b * lighting + tex.b * specular;

            Color out = {
                (unsigned char)fminf(r, 255.0f),
                (unsigned char)fminf(g, 255.0f),
                (unsigned char)fminf(b, 255.0f),
                255
            };

            if (x >= 0 && x < ctx->screenWidth)
                framebuffer[fbY * ctx->screenWidth + x] = out;

            // =======================
            // INCREMENT
            // =======================
            curPx += tri->eqPx.a; curPy += tri->eqPy.a; curPz += tri->eqPz.a;
            curNx += tri->eqNx.a; curNy += tri->eqNy.a; curNz += tri->eqNz.a;
            currentU += tri->eqU.a; currentV += tri->eqV.a;
        }
    }
}

void warnock(RenderContext* ctx, Region* R, int* indices, int count, int depth)
{
    if (!R) return;

    int left   = R->x1;
    int top    = ctx->screenHeight - R->y2;
    int width  = R->x2 - R->x1;
    int height = R->y2 - R->y1;

    if (depth >= ctx->tree_depth) {
        int best = 0;
        float z = ctx->polys[indices[0]].zmin;

        for (int i = 1; i < count; i++)
        {
            if (ctx->polys[indices[i]].zmin < z){
                z = ctx->polys[indices[i]].zmin;
                best = i;
            }
        }
        if(ctx->hybride)
            drawRegionZBuffer(ctx, R, ctx->polys, indices, count);
        else
            if (ctx->texImage.data==NULL)
                DrawRectangleFramebuffer(ctx, left, top, width, height, ctx->polys[indices[best]].couleur);
            else 
                DrawFullShaderRegion(ctx, R, &ctx->polys[indices[best]]);
        return;
    }

    int localIndices[ctx->max_poly];
    int localCount = 0;

    for (int i = 0; i < count; i++) {
        int idx = indices[i];
        if (localCount >= ctx->max_poly) break;
        if (!ctx->polys[idx].visible) continue;

        if (TriangleIntersectsRegion(R, &ctx->polys[idx]))
            localIndices[localCount++] = idx;
    }

    if (localCount == 0) {
        //DrawRectangleLines(left, top, width, height, RED);
        DrawRectangleLinesFramebuffer(ctx, left, top, width, height, RED);
        return;
    }

    if (localCount == 1) {
        Poly* A = &ctx->polys[localIndices[0]];
        // Le triangle couvre tout le rectangle → on peut remplir
        if (region_fully_covered(R, A)) {            
            if (ctx->texImage.data==NULL || ctx->hybride)
                DrawRectangleFramebuffer(ctx, left, top, width, height, A->couleur);
            else 
                DrawFullShaderRegion(ctx, R, A);
            return;
            //DrawRectangle(left, top, width, height, A->couleur);
        }        
        // Le triangle ne couvre qu'une partie → on subdivise
    }

    for (int i = 0; i < localCount; i++) {
        Poly* A = &ctx->polys[localIndices[i]];
        if (region_fully_covered(R, A) && isFrontMost(R, A, ctx->polys, localIndices, localCount)) {
            //DrawRectangle(left, top, width, height, A->couleur);            

            if (ctx->texImage.data==NULL || ctx->hybride)
                DrawRectangleFramebuffer(ctx, left, top, width, height, A->couleur);
            else 
                DrawFullShaderRegion(ctx, R, A);
            return;
        }
    }

    Region regions[4];
    subdivise(R, regions);
    for (int i = 0; i < 4; i++)
        warnock(ctx, &regions[i], localIndices, localCount, depth + 1);
}
