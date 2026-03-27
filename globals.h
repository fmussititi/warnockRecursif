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

// Threads
extern ThreadData* threadData;
extern pthread_t*  threads;

// Barrières
extern pthread_barrier_t barrierStart;
extern pthread_barrier_t barrierEnd;
extern pthread_barrier_t barrierSkyboxDone;
extern pthread_barrier_t barrierBlurPass1;
extern pthread_barrier_t barrierDoFPass1;
extern pthread_barrier_t barrierDoFPass2;
extern pthread_barrier_t barrierTilesDone;

// Tiles
typedef struct {
    int   count;
    int*  indices;
    float minZ;
    float maxZ;
} Tile;

extern Tile* tiles;

#endif // GLOBALS_H
