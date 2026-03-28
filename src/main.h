#ifndef MAIN_H
#define MAIN_H

#include <pthread.h>

typedef struct poly
{
    bool visible;

    Vector2 p0, p1, p2;
    float z0, z1, z2;
    float zmin, zmax;

    float minX, maxX;
    float minY, maxY;

    int tileMinX, tileMaxX;
    int tileMinY, tileMaxY;

    Vector3 n0, n1, n2;  // normales aux 3 sommets
    Vector3 v0, v1, v2;  // positions 3D world space
    Vector2 uv0, uv1, uv2;  // coordonnées UV aux 3 sommets
    Color c0, c1, c2;

    Vector3 tangent;    // vecteur T
    Vector3 bitangent;  // vecteur B

    Color couleur;
} Poly;

typedef struct Region {
    int x1, y1, x2, y2;
} Region;

typedef struct {
    Poly* polys;
    int polyCount;

    Vector3 lightDir;
    float ambient;
    float diffuse;
    float specular;
    float shininess;
    float refl1;
    float refl2;

    Vector3 cameraPos;
    Vector3 cameraForward;
    Vector3 cameraRight;
    Vector3 cameraUp;
    float* skyU;
    float* skyV;

    float far;
    float near;
    float fov;

    Image texImage;
    Image normalMap;    // normal map
    Image envMap;
    int envMap_enable;
    int dof;
    float focalDistance;  // ex: 5.0f  — profondeur au point net
    float focalRange;     // ex: 3.0f  — zone nette (±)
    int   maxBlurRadius;  // ex: 10

    int num_threads;
    int tile_size;
    int max_tri_per_tile;
    int max_tiles;

    int screenWidth;
    int screenHeight;

    int gouraudShading;
    int flatShading;

    int* rootIndices;
    int tree_depth;
    int contour_arbre;
    int max_poly;
} RenderContext;

typedef struct {
    Vector3 worldPos;      
    Vector3 viewPos;       
    Vector2 screenPos;     
    Vector3 normal;        // <--- La normale lissée et transformée
    bool isProjected;      
} CachedVertex;

#endif // MAIN_H