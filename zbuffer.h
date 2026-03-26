#ifndef ZBUFFER_H
#define ZBUFFER_H

#include "raylib.h"
#include "main.h"

void drawTriangleZ(RenderContext* ctx, Poly* tri, float* zbuffer);

#endif // ZBUFFER_H
