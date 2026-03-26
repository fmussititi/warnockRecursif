#ifndef SHADING_H
#define SHADING_H

#include "raylib.h"
#include "raymath.h"
#include "main.h"

void flatShading(RenderContext* ctx, Poly* p, Matrix view);
void gouraudShading(RenderContext* ctx, Poly* p);

#endif // SHADING_H
