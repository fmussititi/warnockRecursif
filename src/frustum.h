#ifndef FRUSTUM_H
#define FRUSTUM_H

#include "raylib.h"
#include "raymath.h"
#include "main.h"
#include <stdbool.h>

typedef struct {
    Vector3 normal;
    float   d;
} Plane;

typedef struct {
    Plane planes[6];
} Frustum;

Frustum extractFrustum(Matrix vp);
bool    aabbInFrustum(Frustum* f, Vector3 aabbMin, Vector3 aabbMax);
bool    triangleInFrustum(Frustum* f, Vector3 v0, Vector3 v1, Vector3 v2);
Matrix  buildProjectionMatrix(RenderContext* ctx);
bool    isBackFace(Vector3 v0, Vector3 v1, Vector3 v2, Vector3 cameraPos);

#endif // FRUSTUM_H
