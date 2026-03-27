#ifndef UTILS_H
#define UTILS_H

#include "raylib.h"
#include "raymath.h"
#include "main.h"

typedef struct {
    float A, B, C;
} EdgeEq;

static inline EdgeEq makeEdge(Vector2 a, Vector2 b) {
    EdgeEq e;
    e.A = a.y - b.y;
    e.B = b.x - a.x;
    e.C = a.x * b.y - a.y * b.x;
    return e;
}

static inline float evalEdge(EdgeEq e, float x, float y) {
    return e.A * x + e.B * y + e.C;
}

Color   couleurAleatoire(void);
Vector3 getNormal(Mesh mesh, int index, Vector3* smoothNormals);
Vector3 getVertex(Mesh mesh, int index);
Vector2 getUV(Mesh mesh, int index);
int     compare_zmin(const void* a, const void* b);

#endif // UTILS_H
