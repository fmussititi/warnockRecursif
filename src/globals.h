#ifndef GLOBALS_H
#define GLOBALS_H

#include "raylib.h"
#include <stdatomic.h>
#include <pthread.h>
#include "main.h"

// Tiles
extern int tilesX;
extern int tilesY;
extern atomic_int tilesRemaining;

// Buffers
extern Color* framebuffer;
extern Color* framebufferBlur;
extern float* depthBuffer;
extern float* zbuffer;

// Threads
typedef struct {
    RenderContext* ctx;
    int startTile;
    int endTile;
    int startLine;
    int endLine;
    int startCol;
    int endCol;
    
// Pointeurs pour l'allocation dynamique
    int *r_sum;
    int *g_sum;
    int *b_sum;

    bool frameReady;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} ThreadData;

extern ThreadData* threadsData;
extern pthread_t*  threads;

// Barrières
extern pthread_barrier_t barrierStart;
extern pthread_barrier_t barrierEnd;
extern pthread_barrier_t barrierSkyboxDone;
extern pthread_barrier_t barrierDoFPass1;
extern pthread_barrier_t barrierTilesDone;

// Tiles
typedef struct {
    int   count;
    int*  indices;
    float minZ;
    float maxZ;
} Tile;

extern Tile* tiles;
extern int* all_indices;

#endif // GLOBALS_H
