#include "globals.h"

int tilesX;
int tilesY;
atomic_int tilesRemaining;

Color* framebuffer   = NULL;
Color* framebufferBlur = NULL;
float* zbuffer       = NULL;

ThreadData* threadsData = NULL;
pthread_t*  threads    = NULL;

pthread_barrier_t barrierStart;
pthread_barrier_t barrierEnd;
pthread_barrier_t barrierSkyboxDone;
pthread_barrier_t barrierDoFPass1;
pthread_barrier_t barrierTilesDone;

Tile* tiles = NULL;
int* all_indices = NULL;
