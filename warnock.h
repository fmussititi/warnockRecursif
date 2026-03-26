#ifndef WARNOCK_H
#define WARNOCK_H

#include "raylib.h"
#include "raymath.h"
#include "main.h"
#include "utils.h"

void subdivise(Region* r, Region regions[4]);
bool TriangleIntersectsRegion(Region* r, Poly* tri);
bool AABBOverlap(Region* r, Poly* p);
int  region_fully_covered(Region* r, Poly* tri);
int  region_outside(Region* r, Poly* tri);
void drawRegionZBuffer(RenderContext* ctx, Region* r, Poly* polys, int* indices, int count);
void warnock(RenderContext* ctx, Region* r, int* indices, int count, int depth);

#endif // WARNOCK_H
