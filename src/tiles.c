#include "tiles.h"
#include "globals.h"
#include "skybox.h"
#include "warnock.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <immintrin.h>

void binTriangles(RenderContext* ctx)
{
    for (int t = 0; t < tilesX * tilesY; t++)
        tiles[t].count = 0;

    for (int i = 0; i < ctx->polyCount; i++) {
        Poly* p = &ctx->polys[i];
        if (!p->visible) continue;

        int minTileX = Clamp(p->tileMinX, 0, tilesX-1);
        int maxTileX = Clamp(p->tileMaxX, 0, tilesX-1);
        int minTileY = Clamp(p->tileMinY, 0, tilesY-1);
        int maxTileY = Clamp(p->tileMaxY, 0, tilesY-1);

        for (int ty = minTileY; ty <= maxTileY; ty++) {
            for (int tx = minTileX; tx <= maxTileX; tx++) {
                int   tileIndex = ty * tilesX + tx;
                Tile* tile      = &tiles[tileIndex];

                Region myTile = {
                    tx * ctx->tile_size,       ty * ctx->tile_size,
                    (tx+1) * ctx->tile_size,   (ty+1) * ctx->tile_size
                };

                if (!AABBOverlap(&myTile, p))              continue;
                if (!TriangleIntersectsRegion(&myTile, p)) continue;

                tile->indices[tile->count++] = i;
            }
        }
    }
}

void drawTile(RenderContext* ctx, int tx, int ty)
{
    int x0   = tx * ctx->tile_size;
    int y0   = ty * ctx->tile_size;
    int x1   = x0 + ctx->tile_size;
    int yEnd = y0 + ctx->tile_size;

    Tile* tile = &tiles[ty * tilesX + tx];
    if (tile->count == 0) return;

    // Reset de la zone du zbuffer global correspondant à cette tile
    for (int y = y0; y < yEnd; y++) {
        int fbY = ctx->screenHeight - y;
        if (fbY < 0 || fbY >= ctx->screenHeight) continue;
        for (int x = x0; x < x1; x++)
            zbuffer[fbY * ctx->screenWidth + x] = 1e9f;
    }

    const bool   hasTexture   = ctx->texImage.data   != NULL;
    const bool   hasNormalMap = ctx->normalMap.data  != NULL;
    const bool   hasEnvMap    = ctx->envMap.data     != NULL;
    const Color* texPixels    = hasTexture   ? (Color*)ctx->texImage.data  : NULL;
    const Color* normalPixels = hasNormalMap ? (Color*)ctx->normalMap.data : NULL;
    const int texW  = ctx->texImage.width;
    const int texH  = ctx->texImage.height;
    const int normW = ctx->normalMap.width;
    const int normH = ctx->normalMap.height;

    for (int t = 0; t < tile->count; t++) {
        Poly* tri = &ctx->polys[tile->indices[t]];

        int minX = (int)fmaxf(x0,     floorf(tri->minX));
        int maxX = (int)fminf(x1-1,   ceilf(tri->maxX));
        int minY = (int)fmaxf(y0,     floorf(tri->minY));
        int maxY = (int)fminf(yEnd-1, ceilf(tri->maxY));

        if (minX > maxX || minY > maxY) continue;

        typedef struct { float A, B, C; } EdgeEq;
        #define MAKE_EDGE(a,b) (EdgeEq){ (a).y-(b).y, (b).x-(a).x, (a).x*(b).y-(a).y*(b).x }
        #define EVAL_EDGE(e,x,y) ((e).A*(x) + (e).B*(y) + (e).C)

        EdgeEq e0 = MAKE_EDGE(tri->p0, tri->p1);
        EdgeEq e1 = MAKE_EDGE(tri->p1, tri->p2);
        EdgeEq e2 = MAKE_EDGE(tri->p2, tri->p0);

        float area    = (tri->p1.x-tri->p0.x)*(tri->p2.y-tri->p0.y) -
                        (tri->p2.x-tri->p0.x)*(tri->p1.y-tri->p0.y);
        float invArea = 1.0f / area;

        float w0_row = EVAL_EDGE(e0, minX+0.5f, minY+0.5f);
        float w1_row = EVAL_EDGE(e1, minX+0.5f, minY+0.5f);
        float w2_row = EVAL_EDGE(e2, minX+0.5f, minY+0.5f);

        __m256 e0A_8  = _mm256_set1_ps(e0.A * 8);
        __m256 e1A_8  = _mm256_set1_ps(e1.A * 8);
        __m256 e2A_8  = _mm256_set1_ps(e2.A * 8);
        __m256 offsets = _mm256_set_ps(7,6,5,4,3,2,1,0);
        __m256 zero    = _mm256_setzero_ps();

        for (int y = minY; y <= maxY; y++) {
            __m256 w0_v = _mm256_add_ps(_mm256_set1_ps(w0_row), _mm256_mul_ps(offsets, _mm256_set1_ps(e0.A)));
            __m256 w1_v = _mm256_add_ps(_mm256_set1_ps(w1_row), _mm256_mul_ps(offsets, _mm256_set1_ps(e1.A)));
            __m256 w2_v = _mm256_add_ps(_mm256_set1_ps(w2_row), _mm256_mul_ps(offsets, _mm256_set1_ps(e2.A)));

            for (int x = minX; x <= maxX; x += 8) {
                __m256 m0   = _mm256_cmp_ps(w0_v, zero, _CMP_GE_OS);
                __m256 m1   = _mm256_cmp_ps(w1_v, zero, _CMP_GE_OS);
                __m256 m2   = _mm256_cmp_ps(w2_v, zero, _CMP_GE_OS);
                __m256 mask = _mm256_and_ps(_mm256_and_ps(m0, m1), m2);
                int imask   = _mm256_movemask_ps(mask);

                if (imask != 0) {
                    float w0_arr[8], w1_arr[8], w2_arr[8];
                    _mm256_storeu_ps(w0_arr, w0_v);
                    _mm256_storeu_ps(w1_arr, w1_v);
                    _mm256_storeu_ps(w2_arr, w2_v);

                    for (int k = 0; k < 8; k++) {
                        if (!(imask & (1 << k))) continue;
                        int px = x + k;
                        if (px > maxX) break;

                        float alpha = w1_arr[k] * invArea;
                        float beta  = w2_arr[k] * invArea;
                        float gamma = 1.0f - alpha - beta;
                        float z     = alpha*tri->z0 + beta*tri->z1 + gamma*tri->z2;

                        // coordonnées écran absolues
                        int fbY   = ctx->screenHeight - y;
                        int index = fbY * ctx->screenWidth + px;

                        if (z < zbuffer[index]) {
                            zbuffer[index] = z;

                            Vector3 n = {
                                alpha*tri->n0.x + beta*tri->n1.x + gamma*tri->n2.x,
                                alpha*tri->n0.y + beta*tri->n1.y + gamma*tri->n2.y,
                                alpha*tri->n0.z + beta*tri->n1.z + gamma*tri->n2.z,
                            };
                            n = Vector3Normalize(n);
                            n = Vector3Negate(n);

                            Vector3 pixelPos = {
                                alpha*tri->v0.x + beta*tri->v1.x + gamma*tri->v2.x,
                                alpha*tri->v0.y + beta*tri->v1.y + gamma*tri->v2.y,
                                alpha*tri->v0.z + beta*tri->v1.z + gamma*tri->v2.z,
                            };

                            float u = alpha*tri->uv0.x + beta*tri->uv1.x + gamma*tri->uv2.x;
                            float v = alpha*tri->uv0.y + beta*tri->uv1.y + gamma*tri->uv2.y;

                            if (hasNormalMap) {
                                int nx = (int)(u * normW) % normW;
                                int ny = (int)(v * normH) % normH;
                                if (nx < 0) nx += normW;
                                if (ny < 0) ny += normH;
                                Color nc = normalPixels[ny * normW + nx];
                                Vector3 nMap = {
                                    (nc.r/255.0f)*2.0f-1.0f,
                                    (nc.g/255.0f)*2.0f-1.0f,
                                    (nc.b/255.0f)*2.0f-1.0f
                                };
                                n = Vector3Normalize((Vector3){
                                    tri->tangent.x*nMap.x + tri->bitangent.x*nMap.y + n.x*nMap.z,
                                    tri->tangent.y*nMap.x + tri->bitangent.y*nMap.y + n.y*nMap.z,
                                    tri->tangent.z*nMap.x + tri->bitangent.z*nMap.y + n.z*nMap.z
                                });
                            }

                            Color base;
                            if (hasTexture) {
                                int texX = (int)(u * texW) % texW;
                                int texY = (int)(v * texH) % texH;
                                if (texX < 0) texX += texW;
                                if (texY < 0) texY += texH;
                                base = texPixels[texY * texW + texX];
                            } else {
                                base = tri->couleur;
                            }

                            Vector3 viewDir = Vector3Normalize(Vector3Subtract(ctx->cameraPos, pixelPos));

                            Color envColor = base;
                            if (hasEnvMap) {
                                Vector3 I = Vector3Negate(viewDir);
                                Vector3 R = Vector3Subtract(I, Vector3Scale(n, 2.0f * Vector3DotProduct(I, n)));
                                R = Vector3Normalize(R);
                                envColor = SampleEquirectangular(ctx, R);
                                envColor.r = (unsigned char)fminf(envColor.r * 1.3f + 20, 255);
                                envColor.g = (unsigned char)fminf(envColor.g * 1.3f + 20, 255);
                                envColor.b = (unsigned char)fminf(envColor.b * 1.3f + 20, 255);
                            }

                            float fresnel = powf(1.0f - fmaxf(Vector3DotProduct(n, viewDir), 0.0f), 5.0f);
                            float dotNL   = fmaxf(Vector3DotProduct(n, ctx->lightDir), 0.0f);
                            float diffuse = dotNL;
                            Vector3 halfDir = Vector3Normalize(Vector3Add(ctx->lightDir, viewDir));
                            float dotNH = fmaxf(Vector3DotProduct(n, halfDir), 0.0f);
                            float spec  = (dotNL > 0.0f) ? powf(dotNH, ctx->shininess) : 0.0f;
                            float lighting = fminf(ctx->ambient + diffuse * ctx->diffuse, 1.0f);

                            Color litBase = {
                                (unsigned char)(base.r * lighting),
                                (unsigned char)(base.g * lighting),
                                (unsigned char)(base.b * lighting),
                                255
                            };

                            Color final;
                            if (hasEnvMap) {
                                float reflectivity = ctx->refl1 + ctx->refl2 * fresnel;
                                final = (Color){
                                    (unsigned char)(litBase.r*(1.0f-reflectivity) + envColor.r*reflectivity),
                                    (unsigned char)(litBase.g*(1.0f-reflectivity) + envColor.g*reflectivity),
                                    (unsigned char)(litBase.b*(1.0f-reflectivity) + envColor.b*reflectivity),
                                    255
                                };
                            } else {
                                final = litBase;
                            }

                            float specIntensity = spec * ctx->specular;
                            final.r = (unsigned char)fminf(final.r + 255*specIntensity, 255);
                            final.g = (unsigned char)fminf(final.g + 255*specIntensity, 255);
                            final.b = (unsigned char)fminf(final.b + 255*specIntensity, 255);

                            int fbY = ctx->screenHeight - y;
                            if (fbY >= 0 && fbY < ctx->screenHeight) {
                                framebuffer[fbY * ctx->screenWidth + px] = final;
                            }
                        }
                    }
                }

                w0_v = _mm256_add_ps(w0_v, e0A_8);
                w1_v = _mm256_add_ps(w1_v, e1A_8);
                w2_v = _mm256_add_ps(w2_v, e2A_8);
            }

            w0_row += e0.B;
            w1_row += e1.B;
            w2_row += e2.B;
        }
    }
}

void* worker(void* arg)
{
    ThreadData* td = (ThreadData*)arg;
    int W = td->ctx->screenWidth;
    int H = td->ctx->screenHeight;

    while (true) {
        pthread_barrier_wait(&barrierStart);

        // ── 1. SKYBOX ────────────────────────────────────────────────────────
        if (td->ctx->skyU && td->ctx->skyV && td->ctx->envMap.data) {
            for (int y = td->startLine; y < td->endLine; y++) {
                if (y < 0 || y >= H) continue;
                int screenY = H - 1 - y;
                int base    = screenY * W;
                int lutRow  = y * W;
                for (int x = 0; x < W; x++)
                    framebuffer[base + x] = SampleEquirectangularUV(td->ctx,
                        td->ctx->skyU[lutRow + x],
                        td->ctx->skyV[lutRow + x]);
            }
        } else {
            Color clearColor = (Color){ 20, 20, 30, 255 };
            for (int y = td->startLine; y < td->endLine; y++) {
                if (y < 0 || y >= H) continue;
                int screenY = H - 1 - y;
                Color* rowPtr = &framebuffer[screenY * W];
                for (int x = 0; x < W; x++)
                    rowPtr[x] = clearColor;
            }
        }

        // ── 2. RESET ZBUFFER (distribué sur tous les threads) ───────────
        if (td->ctx->dof) {
            for (int y = td->startLine; y < td->endLine; y++) {
                int row = y * W;
                for (int x = 0; x < W; x++)
                    zbuffer[row + x] = 1e9f;
            }
        }

        // Synchronisation : skybox + reset depth terminés
        pthread_barrier_wait(&barrierSkyboxDone);

        // ── 3. TRIANGLES ─────────────────────────────────────────────────────
        for (int t = td->startTile; t < td->endTile; t++) {
            int tx = t % tilesX;
            int ty = t / tilesX;
            drawTile(td->ctx, tx, ty);
        }

        pthread_barrier_wait(&barrierTilesDone);

        // ── 4. DOF OPTIMISÉ (2 PASSES O(1)) ──────────────────────────────────────
        if (td->ctx->dof) {
            int   maxR          = td->ctx->maxBlurRadius;
            float focalDistance = td->ctx->focalDistance;
            float focalRange    = td->ctx->focalRange;

            // ── PASS 1 : HORIZONTAL (framebuffer -> framebufferBlur) ──────────────
            for (int y = td->startLine; y < td->endLine; y++) {
                if (y < 0 || y >= H) continue;
                int row = y * W;

                // Accumulation locale à la ligne
                int curR = 0, curG = 0, curB = 0;
                for (int x = 0; x < W; x++) {
                    Color c = framebuffer[row + x];
                    curR += c.r; curG += c.g; curB += c.b;
                    td->r_sum[x] = curR; td->g_sum[x] = curG; td->b_sum[x] = curB;
                }

                for (int x = 0; x < W; x++) {
                    float depth = zbuffer[row + x];
                    int radius = (int)(fminf(fabsf(depth - focalDistance) / focalRange, 1.0f) * maxR);

                    if (radius <= 0) {
                        framebufferBlur[row + x] = framebuffer[row + x];
                        continue;
                    }

                    int x1 = x - radius - 1;
                    int x2 = x + radius;
                    if (x2 >= W) x2 = W - 1;
                    int count = x2 - (x1 < 0 ? -1 : x1);

                    int r = (x1 < 0) ? td->r_sum[x2] : td->r_sum[x2] - td->r_sum[x1];
                    int g = (x1 < 0) ? td->g_sum[x2] : td->g_sum[x2] - td->g_sum[x1];
                    int b = (x1 < 0) ? td->b_sum[x2] : td->b_sum[x2] - td->b_sum[x1];
                    framebufferBlur[row + x] = (Color){ r/count, g/count, b/count, 255 };
                }
            }

            pthread_barrier_wait(&barrierDoFPass1);

            // ── PASS 2 : VERTICAL (framebufferBlur -> framebuffer) ── O(1) ──────────
            for (int x = td->startCol; x < td->endCol; x++) {  // ← découper par colonnes !

                // Sommes préfixes verticales sur la colonne
                int curR = 0, curG = 0, curB = 0;
                for (int y = 0; y < H; y++) {
                    Color c = framebufferBlur[y * W + x];
                    curR += c.r; curG += c.g; curB += c.b;
                    td->r_sum[y] = curR;
                    td->g_sum[y] = curG;
                    td->b_sum[y] = curB;
                }

                for (int y = 0; y < H; y++) {
                    float depth  = zbuffer[y * W + x];
                    int   radius = (int)(fminf(fabsf(depth - focalDistance) / focalRange, 1.0f) * maxR);

                    if (radius <= 0) continue;

                    int y1    = y - radius - 1;
                    int y2    = (y + radius < H) ? y + radius : H - 1;
                    int count = y2 - (y1 < 0 ? -1 : y1);

                    int r = (y1 < 0) ? td->r_sum[y2] : td->r_sum[y2] - td->r_sum[y1];
                    int g = (y1 < 0) ? td->g_sum[y2] : td->g_sum[y2] - td->g_sum[y1];
                    int b = (y1 < 0) ? td->b_sum[y2] : td->b_sum[y2] - td->b_sum[y1];
                    framebuffer[y * W + x] = (Color){ r/count, g/count, b/count, 255 };
                }
            }
        }
        pthread_barrier_wait(&barrierEnd);
    }
    return NULL;
}

void initThreads(RenderContext* ctx)
{
    int totalTiles     = tilesX * tilesY;
    int tilesPerThread = (totalTiles + ctx->num_threads - 1) / ctx->num_threads;
    int linesPerThread = ctx->screenHeight / ctx->num_threads;
    int colsPerThread  = (ctx->screenWidth + ctx->num_threads - 1) / ctx->num_threads;

    int max_size = ctx->screenWidth + ctx->screenHeight;

    for (int i = 0; i < ctx->num_threads; i++) {
        threadsData[i].ctx       = ctx;
        
        threadsData[i].startTile = i * tilesPerThread;
        threadsData[i].endTile   = (i == ctx->num_threads-1) ? totalTiles : (i+1)*tilesPerThread;

        threadsData[i].startLine = i * linesPerThread;
        threadsData[i].endLine   = (i == ctx->num_threads-1) ? ctx->screenHeight : (i+1)*linesPerThread;

        threadsData[i].startCol = i * colsPerThread;
        threadsData[i].endCol   = (i == ctx->num_threads-1)
                                ? ctx->screenWidth
                                : (i+1) * colsPerThread;

        threadsData[i].r_sum = (int*)malloc(max_size * sizeof(int));
        threadsData[i].g_sum = (int*)malloc(max_size * sizeof(int));
        threadsData[i].b_sum = (int*)malloc(max_size * sizeof(int));

        // Toujours vérifier si le malloc a réussi
        if (!threadsData[i].r_sum || !threadsData[i].g_sum || !threadsData[i].b_sum) {
            fprintf(stderr, "Erreur d'allocation mémoire pour le thread %d\n", i);
            exit(1);
        }
        threadsData[i].frameReady = false;
        pthread_mutex_init(&threadsData[i].mutex, NULL);
        pthread_cond_init(&threadsData[i].cond,   NULL);
        pthread_create(&threads[i], NULL, worker, &threadsData[i]);
    }
}

void renderFrame(RenderContext* ctx)
{
    for (int i = 0; i < ctx->num_threads; i++)
        threadsData[i].ctx = ctx;

    pthread_barrier_wait(&barrierStart);

    pthread_barrier_wait(&barrierSkyboxDone);

    pthread_barrier_wait(&barrierTilesDone);

    if (ctx->dof) {
        pthread_barrier_wait(&barrierDoFPass1);
    }

    pthread_barrier_wait(&barrierEnd);
}
