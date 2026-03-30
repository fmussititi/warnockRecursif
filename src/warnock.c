#include "warnock.h"
#include "utils.h"
#include "globals.h"
#include <math.h>
#include <stdbool.h>

void subdivise(Region* r, Region regions[4]) {
    regions[0] = (Region){r->x1,             (r->y1+r->y2)/2, (r->x1+r->x2)/2, r->y2};
    regions[1] = (Region){(r->x1+r->x2)/2,   (r->y1+r->y2)/2, r->x2,           r->y2};
    regions[2] = (Region){r->x1,             r->y1,           (r->x1+r->x2)/2, (r->y1+r->y2)/2};
    regions[3] = (Region){(r->x1+r->x2)/2,   r->y1,           r->x2,           (r->y1+r->y2)/2};
}

// Fonction utilitaire pour tester si un rectangle est du côté "extérieur" d'une arête
// On prend le coin du rectangle qui maximise la fonction de distance signée.
static inline bool IsRegionOutsideEdge(Region* r, EdgeLine* e) {
    float val = e->C;
    // On choisit le coin (x,y) qui donne la valeur maximale pour Ax + By + C
    val += (e->A > 0) ? e->A * r->x2 : e->A * r->x1;
    val += (e->B > 0) ? e->B * r->y2 : e->B * r->y1;
    
    // Si même le point le plus "favorable" est < 0, le rectangle est hors du demi-plan
    return val < 0; 
}

bool TriangleIntersectsRegion(Region* r, Poly* tri) {
    // 1. Test AABB (Largeur/Hauteur) - Le plus rapide
    if (tri->maxX < r->x1 || tri->minX > r->x2 ||
        tri->maxY < r->y1 || tri->minY > r->y2) return false;

    for (int i = 0; i < 3; i++) {

        // 3. Si la région est totalement à l'extérieur d'une seule arête, 
        // alors il n'y a pas d'intersection (Théorème de l'axe séparateur).
        if (IsRegionOutsideEdge(r, &tri->lines[i])) return false;
    }

    // 4. Si on passe les tests ci-dessus, il y a soit intersection, 
    // soit le rectangle est entièrement DANS le triangle.
    return true;
}

bool AABBOverlap(Region* r, Poly* p) {
    return !(p->maxX < r->x1 || p->minX > r->x2 ||
             p->maxY < r->y1 || p->minY > r->y2);
}

int region_fully_covered(Region* r, Poly* tri) {
// On teste les 3 arêtes du triangle
    for (int i = 0; i < 3; i++) {
        EdgeLine* e = &tri->lines[i];
        
        // Pour que la région soit entièrement couverte, il faut que le coin
        // le PLUS PROCHE de l'extérieur soit quand même à l'intérieur.
        // On cherche donc le coin (x,y) qui MINIMISE Ax + By + C.
        float min_val = e->C;
        min_val += (e->A > 0) ? e->A * r->x1 : e->A * r->x2;
        min_val += (e->B > 0) ? e->B * r->y1 : e->B * r->y2;

        // Si le pire coin est < 0, alors au moins une partie du rectangle
        // est en dehors de cette arête -> Pas de couverture totale.
        if (min_val < 0) return 0;
    }

    return 1;
}

void drawRegionZBuffer(RenderContext* ctx, Region* r, Poly* polys, int* indices, int count)
{
    int width  = r->x2 - r->x1;
    int height = r->y2 - r->y1;

    float zbuf[width * height];
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

                    int lx    = x - r->x1;
                    int ly    = y - r->y1;
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

static bool isADevantB(Region* r, Poly* A, Poly* B) {
    // Test rapide : si la boîte Z de A est devant B, pas besoin de calculs de plans
    if (A->zmax < B->zmin) return true;
    // Si la boîte Z de B est devant A, A ne peut pas être devant
    if (B->zmax < A->zmin) return false;

    // Test des 4 coins de la région
    float cornersX[4] = {(float)r->x1, (float)r->x2, (float)r->x1, (float)r->x2};
    float cornersY[4] = {(float)r->y1, (float)r->y1, (float)r->y2, (float)r->y2};

    for (int i = 0; i < 4; i++) {
        float zA = GetZAt(A, cornersX[i], cornersY[i]);
        float zB = GetZAt(B, cornersX[i], cornersY[i]);

        // Si à n'importe quel coin le Z de A est derrière celui de B, 
        // A n'est pas "FrontMost" (ou ils s'intersectent).
        if (zA > zB) return false;
    }

    return true;
}


static int isFrontMost(Region* r, Poly* A, Poly* polys, int* indices, int count)
{
    for (int i = 0; i < count; i++) {
        Poly* B = &polys[indices[i]];

        if (B == A) continue;

        // Si A n'est pas devant B sur toute la surface du rectangle r,
        // on renvoie 0 (ce qui forcera Warnock à subdiviser)
        if (!isADevantB(r, A, B)) {
            return 0;
        }
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
            drawRegionZBuffer(ctx, r, ctx->polys, indices, count);
        else
            DrawRectangleFramebuffer(ctx, left, top, width, height, ctx->polys[indices[best]].couleur);
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
        //DrawRectangleLines(left, top, width, height, RED);
        DrawRectangleLinesFramebuffer(ctx, left, top, width, height, RED);
        return;
    }

    if (localCount == 1) {
        Poly* A = &ctx->polys[localIndices[0]];
        // Le triangle couvre tout le rectangle → on peut remplir
        if (region_fully_covered(r, A)) {            
            DrawRectangleFramebuffer(ctx, left, top, width, height, A->couleur);
            //DrawRectangle(left, top, width, height, A->couleur);
            return;
        }        
        // Le triangle ne couvre qu'une partie → on subdivise
    }

    for (int i = 0; i < localCount; i++) {
        Poly* A = &ctx->polys[localIndices[i]];
        if (region_fully_covered(r, A) && isFrontMost(r, A, ctx->polys, localIndices, localCount)) {
            //DrawRectangle(left, top, width, height, A->couleur);
            DrawRectangleFramebuffer(ctx, left, top, width, height, A->couleur);
            return;
        }
    }

    Region regions[4];
    subdivise(r, regions);
    for (int i = 0; i < 4; i++)
        warnock(ctx, &regions[i], localIndices, localCount, depth + 1);
}
