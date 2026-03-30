#ifndef WARNOCK_H
#define WARNOCK_H

#include "raylib.h"
#include "raymath.h"
#include "main.h"
#include "utils.h"

void subdivise(Region* R, Region regions[4]);
bool TriangleIntersectsRegion(Region* R, Poly* tri);
bool AABBOverlap(Region* R, Poly* p);
int  region_fully_covered(Region* R, Poly* tri);
static inline float GetZAt(Poly* tri, float x, float y);
static bool isADevantB(Region* R, Poly* A, Poly* B);
static int isFrontMost(Region* R, Poly* A, Poly* polys, int* indices, int count);
void drawRegionZBuffer(RenderContext* ctx, Region* R, Poly* polys, int* indices, int count);
void warnock(RenderContext* ctx, Region* R, int* indices, int count, int depth);

#endif // WARNOCK_H
