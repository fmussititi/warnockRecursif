#include "utils.h"
#include <math.h>

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
