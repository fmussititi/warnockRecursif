#ifndef SKYBOX_H
#define SKYBOX_H

#include "raylib.h"
#include "raymath.h"
#include "main.h"

Vector3 GetRayDirection(RenderContext* ctx, int px, int py);
void    PrecomputeSkyboxLUT(RenderContext* ctx);
Color   SampleEquirectangular(RenderContext* ctx, Vector3 dir);
Color   SampleEquirectangularUV(RenderContext* ctx, float u, float v);

#endif // SKYBOX_H
