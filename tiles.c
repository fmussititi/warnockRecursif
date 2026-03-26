#include "tiles.h"
#include "globals.h"
#include "skybox.h"
#include "warnock.h"
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

    float zbuf[ctx->tile_size * ctx->tile_size];
    for (int i = 0; i < ctx->tile_size * ctx->tile_size; i++)
        zbuf[i] = 1e9f;

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

                        int lx    = px - x0;
                        int ly    = y  - y0;
                        int index = ly * ctx->tile_size + lx;

                        if (z < zbuf[index]) {
                            zbuf[index] = z;

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
                                depthBuffer[fbY * ctx->screenWidth + px] = z;
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

        // 1. PHASE SKYBOX (Remplissage initial)
        if (td->ctx->skyU && td->ctx->skyV && td->ctx->envMap.data) {
            for (int y = td->startLine; y < td->endLine; y++) {
                if (y < 0 || y >= H) continue;
                int screenY = H - 1 - y; // Correction de l'inversion Y si nécessaire
                int base = screenY * W;
                int lutRow = y * W;
                for (int x = 0; x < W; x++) {
                    framebuffer[base + x] = SampleEquirectangularUV(td->ctx, 
                        td->ctx->skyU[lutRow + x], td->ctx->skyV[lutRow + x]);
                }
            }
        }else {
            // Cas sans Skybox : On remplit avec une couleur par défaut (ex: noir ou bleu nuit)
            // Couleur personnalisée
            Color clearColor = (Color){ 20, 20, 30, 255 };
            
            for (int y = td->startLine; y < td->endLine; y++) {
                if (y < 0 || y >= H) continue;
                int screenY = H - 1 - y;
                Color* rowPtr = &framebuffer[screenY * W];
                
                // Cette boucle est généralement "vectorisée" par le compilateur
                // ce qui la rend presque aussi rapide qu'un memset
                for (int x = 0; x < W; x++) {
                    rowPtr[x] = clearColor;
                }
            }
        }

        // 2. RESET DEPTH BUFFER (Seulement si DoF actif)
        // On initialise à l'infini pour que la Skybox soit traitée par le DoF
        if (td->ctx->dof && td->startLine == 0) {
            for (int i = 0; i < W * H; i++) depthBuffer[i] = 1e9f;
        }

        if (td->ctx->envMap.data) pthread_barrier_wait(&barrierSkyboxDone);

        // 3. PHASE TRIANGLES (Dessin par-dessus la skybox)
        for (int t = td->startTile; t < td->endTile; t++) {
            int tx = t % tilesX;
            int ty = t / tilesX;

            drawTile(td->ctx, tx, ty); // Remplit framebuffer ET depthBuffer
        }

        pthread_barrier_wait(&barrierTilesDone);

        // 4. PHASE DOF (Appliquée à TOUT l'écran : Objets + Skybox)
        if (td->ctx->dof) {
            int maxR = td->ctx->maxBlurRadius;

            // PASS 1 : Horizontal (framebuffer -> framebufferBlur)
            for (int y = td->startLine; y < td->endLine; y++) {
                int row = y * W;
                for (int x = 0; x < W; x++) {
                    float depth = depthBuffer[row + x];
                    // Calcul du CoC : si depth est 1e9, coc sera max (1.0)
                    float coc = fabsf(depth - td->ctx->focalDistance) / td->ctx->focalRange;
                    int radius = (int)(fminf(coc, 1.0f) * maxR);

                    if (radius <= 0) {
                        framebufferBlur[row + x] = framebuffer[row + x];
                        continue;
                    }

                    int r=0, g=0, b=0, count=0;
                    for (int dx = -radius; dx <= radius; dx++) {
                        int nx = x + dx;
                        if (nx >= 0 && nx < W) {
                            Color c = framebuffer[row + nx];
                            r += c.r; g += c.g; b += c.b; count++;
                        }
                    }
                    framebufferBlur[row + x] = (Color){ r/count, g/count, b/count, 255 };
                }
            }

            pthread_barrier_wait(&barrierDoFPass1);

            // PASS 2 : Vertical (framebufferBlur -> framebuffer)
            for (int y = td->startLine; y < td->endLine; y++) {
                for (int x = 0; x < W; x++) {
                    float depth = depthBuffer[y * W + x];
                    float coc = fabsf(depth - td->ctx->focalDistance) / td->ctx->focalRange;
                    int radius = (int)(fminf(coc, 1.0f) * maxR);

                    if (radius <= 0) continue;

                    int r=0, g=0, b=0, count=0;
                    for (int dy = -radius; dy <= radius; dy++) {
                        int ny = y + dy;
                        if (ny >= 0 && ny < H) {
                            Color c = framebufferBlur[ny * W + x];
                            r += c.r; g += c.g; b += c.b; count++;
                        }
                    }
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

    for (int i = 0; i < ctx->num_threads; i++) {
        threadData[i].ctx       = ctx;
        threadData[i].startTile = i * tilesPerThread;
        threadData[i].endTile   = (i == ctx->num_threads-1) ? totalTiles : (i+1)*tilesPerThread;
        threadData[i].startLine = i * linesPerThread;
        threadData[i].endLine   = (i == ctx->num_threads-1) ? ctx->screenHeight : (i+1)*linesPerThread;
        threadData[i].frameReady = false;
        pthread_mutex_init(&threadData[i].mutex, NULL);
        pthread_cond_init(&threadData[i].cond,   NULL);
        pthread_create(&threads[i], NULL, worker, &threadData[i]);
    }
}

void renderFrame(RenderContext* ctx)
{
    for (int i = 0; i < ctx->num_threads; i++)
        threadData[i].ctx = ctx;

    pthread_barrier_wait(&barrierStart);

    if (ctx->envMap.data)
        pthread_barrier_wait(&barrierSkyboxDone);

    pthread_barrier_wait(&barrierTilesDone);

    if (ctx->dof) {
        pthread_barrier_wait(&barrierDoFPass1);
    }

    pthread_barrier_wait(&barrierEnd);
}
