#ifndef TILES_H
#define TILES_H

#include "raylib.h"
#include "main.h"

void binTriangles(RenderContext* ctx);
void drawTile(RenderContext* ctx, int tx, int ty);
void initThreads(RenderContext* ctx);
void renderFrame(RenderContext* ctx);
void* worker(void* arg);

#endif // TILES_H
