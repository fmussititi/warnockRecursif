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

    Vector3 cameraPos;
    float far;
    float near;
    float fov;

    Image texImage;
    Image normalMap;    // normal map
    Image envMap;
    int num_threads;
    int tile_size;

    int screenWidth;
    int screenHeight;

    int* rootIndices;
    int tree_depth;
    int contour_arbre;
    int max_poly;
} RenderContext;

typedef struct {
    RenderContext* ctx;
    int startTile;
    int endTile;
    bool frameReady;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} ThreadData;

void subdivise(Region* r, Region regions[4]);

int point_in_triangle(Vector2 p, Poly* tri);

int region_fully_covered(Region* r, Poly* tri);

int region_outside(Region* r, Poly* tri);

void drawRegionZBuffer(RenderContext* ctx, Region* r, Poly* polys, int* indices, int count);

void warnock(RenderContext* ctx, Region* r, int* indices, int count, int depth);