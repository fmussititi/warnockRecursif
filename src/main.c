#include "raylib.h"
#include "raymath.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "main.h"
#include "config.h"
#include "globals.h"
#include "utils.h"
#include "skybox.h"
#include "shading.h"
#include "warnock.h"
#include "zbuffer.h"
#include "tiles.h"
#include "frustum.h"

int main(void)
{
    Config cfg = loadConfig("config.ini");

    if (cfg.warnock && !cfg.zbuffer && !cfg.tiles)
        InitWindow(cfg.screen_width, cfg.screen_height,
            TextFormat("Hybride : Warnock + ZBuffer rendering %dx%d",
                cfg.screen_width, cfg.screen_height));
    else
    if (cfg.zbuffer && !cfg.warnock && !cfg.tiles)
        InitWindow(cfg.screen_width, cfg.screen_height,
            TextFormat("ZBuffer rendering %dx%d",
                cfg.screen_width, cfg.screen_height));
    else
    if (cfg.tiles && !cfg.warnock && !cfg.zbuffer)
        InitWindow(cfg.screen_width, cfg.screen_height,
            TextFormat("SoftRender3D - Tiles %dx%d - %d threads",
                cfg.screen_width, cfg.screen_height, cfg.num_threads));
    else return 1;

    if (cfg.tiles){

        tilesX = cfg.screen_width  / cfg.tile_size;
        tilesY = cfg.screen_height / cfg.tile_size;

        framebuffer = malloc(cfg.screen_width * cfg.screen_height * sizeof(Color));
        if (!framebuffer) { printf("ERREUR: malloc framebuffer failed!\n"); return 1; }

        framebufferBlur = malloc(cfg.screen_width * cfg.screen_height * sizeof(Color));
        if (!framebufferBlur) { printf("ERREUR: malloc framebufferBlur failed!\n"); return 1; }

        depthBuffer = malloc(cfg.screen_width * cfg.screen_height * sizeof(float));
        if (!depthBuffer) { printf("ERREUR: malloc depthBuffer failed!\n"); return 1; }

        // 1. Calculer le nombre de tuiles nécessaires
        int tilesX = (cfg.screen_width  + cfg.tile_size - 1) / cfg.tile_size;
        int tilesY = (cfg.screen_height + cfg.tile_size - 1) / cfg.tile_size;
        int total_tiles = tilesX * tilesY;

        // 2. Mettre à jour la config pour que le reste du code soit cohérent
        cfg.max_tiles = total_tiles; 

        // 3. Allocation principale
        tiles = malloc(cfg.max_tiles * sizeof(Tile));
        if (!tiles) { 
            fprintf(stderr, "ERREUR: malloc %d tiles failed!\n", cfg.max_tiles); 
            return 1; 
        }

        // Alloue TOUS les indices d'un coup
        int* all_indices = malloc(cfg.max_tiles * cfg.max_tri_per_tile * sizeof(int));
        if (!all_indices) { 
            fprintf(stderr, "ERREUR: malloc %d all_indices failed!\n", cfg.max_tiles * cfg.max_tri_per_tile); 
            return 1; 
        }

        // 4. Allocation des listes d'indices
        for (int i = 0; i < cfg.max_tiles; i++) {
            tiles[i].indices = &all_indices[i * cfg.max_tri_per_tile];;
            tiles[i].count = 0;
            tiles[i].minZ  = 1e9f;
            tiles[i].maxZ  = -1e9f;
        }

        printf("max_tri_per_tile : %d\n", cfg.max_tri_per_tile);
        printf("max_tiles : %d\n", cfg.max_tiles);

        threadsData = malloc(cfg.num_threads * sizeof(ThreadData));
        if (!threadsData) { printf("ERREUR: malloc threadData failed!\n"); return 1; }

        threads = malloc(cfg.num_threads * sizeof(pthread_t));
        if (!threads) { printf("ERREUR: malloc threads failed!\n"); return 1; }
    }

    // ── Caméra ────────────────────────────────────────────────────────────────
    Camera3D camera = { 0 };
    camera.position   = (Vector3){ cfg.cam_x,    cfg.cam_y,    cfg.cam_z    };
    camera.target     = (Vector3){ cfg.target_x, cfg.target_y, cfg.target_z };
    camera.up         = (Vector3){ 0.0f, -1.0f, 0.0f };
    camera.fovy       = cfg.fov;
    camera.projection = CAMERA_PERSPECTIVE;

    Model model      = LoadModel(cfg.model_path);
    Mesh  mesh       = model.meshes[0];
    int   vertexCount = mesh.vertexCount;

    float rotX = 0.0f, rotY = 0.0f, rotZ = 0.0f;
    SetTargetFPS(60);

    // ── RenderContext ─────────────────────────────────────────────────────────
    RenderContext ctx;
    memset(&ctx, 0, sizeof(RenderContext));

    ctx.lightDir      = Vector3Normalize((Vector3){ cfg.light_x, cfg.light_y, cfg.light_z });
    ctx.cameraPos     = camera.position;
    ctx.screenHeight  = cfg.screen_height;
    ctx.screenWidth   = cfg.screen_width;
    ctx.tree_depth    = cfg.tree_depth;
    ctx.contour_arbre = cfg.contour_arbre;
    ctx.max_poly      = cfg.max_poly;
    ctx.ambient       = cfg.ambient;
    ctx.diffuse       = cfg.diffuse;
    ctx.shininess     = cfg.shininess;
    ctx.specular      = cfg.specular;
    ctx.refl1         = cfg.refl1;
    ctx.refl2         = cfg.refl2;
    ctx.far           = cfg.far;
    ctx.near          = cfg.near;
    ctx.fov           = cfg.fov;
    ctx.num_threads   = cfg.num_threads;
    ctx.tile_size     = cfg.tile_size;
    ctx.max_tri_per_tile = cfg.max_tri_per_tile;
    ctx.max_tiles     = cfg.max_tiles;
    ctx.envMap_enable = cfg.envMap_enable;
    ctx.dof           = cfg.dof;
    ctx.maxBlurRadius = cfg.maxBlurRadius;
    ctx.focalDistance = cfg.focalDistance;
    ctx.focalRange    = cfg.focalRange;

    
    // ── Smooth normals ────────────────────────────────────────────────────────
    Vector3* smoothNormals = calloc(vertexCount, sizeof(Vector3));
    Poly*    PolyList      = malloc(mesh.vertexCount/3 * sizeof(Poly));
    Poly*    visiblePolys  = malloc(mesh.triangleCount * sizeof(Poly));
    float*   zbuffer       = NULL;
    Image    img;
    Texture2D tex;

    for (int i = 0; i < mesh.vertexCount / 3; i++) {
        PolyList[i].visible = false;
        PolyList[i].couleur = couleurAleatoire();
    }


    if (mesh.normals != NULL) {
        // 1. Si c'est une soupe, on optimise
        if (mesh.indices == NULL) {
            printf("Optimisation de la soupe de triangles...\n");
            
            Mesh smoothMesh = OptimizeMesh(mesh); // On crée le nouveau
            
            // LIBÉRATION SÉCURISÉE
            // On ne décharge pas tout de suite si on veut encore lire les anciennes données
            // ou alors on rafraîchit la référence immédiatement.
            UnloadMesh(model.meshes[0]); 
            model.meshes[0] = smoothMesh;
            
            // TRÈS IMPORTANT : On met à jour notre variable locale 'mesh'
            mesh = model.meshes[0]; 
        }

        // 2. Maintenant on peut allouer et remplir smoothNormals
        // Attention : utilise le NOUVEAU vertexCount (qui est plus petit)
        smoothNormals = realloc(smoothNormals, mesh.vertexCount * sizeof(Vector3));

        for (int i = 0; i < mesh.vertexCount; i++) {
            smoothNormals[i] = (Vector3){
                mesh.normals[i*3], 
                mesh.normals[i*3+1], 
                mesh.normals[i*3+2]
            };
        }
        printf("Succès : %d normales lissées extraites.\n", mesh.vertexCount);
    }


    if (cfg.tiles){
        if (cfg.envMap_enable) {
            ctx.envMap = LoadImage(cfg.envMap);
            ImageFormat(&ctx.envMap, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        } else {
            ctx.envMap.data = NULL; ctx.envMap.width = 0; ctx.envMap.height = 0;
        }

        if (cfg.textures_enabled) {
            ctx.texImage  = LoadImage(cfg.tex_diffuse);
            ImageFormat(&ctx.texImage,  PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
            ctx.normalMap = LoadImage(cfg.tex_normal);
            ImageFormat(&ctx.normalMap, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        } else {
            ctx.texImage.data = NULL;
            ctx.normalMap.data = NULL;
        }

        ctx.skyU = malloc(cfg.screen_width * cfg.screen_height * sizeof(float));
        ctx.skyV = malloc(cfg.screen_width * cfg.screen_height * sizeof(float));

        img = GenImageColor(cfg.screen_width, cfg.screen_height, RAYWHITE);
        tex = LoadTextureFromImage(img);
        UnloadImage(img);

        pthread_barrier_init(&barrierStart,      NULL, cfg.num_threads + 1);
        pthread_barrier_init(&barrierEnd,        NULL, cfg.num_threads + 1);
        pthread_barrier_init(&barrierSkyboxDone, NULL, cfg.num_threads + 1);
        pthread_barrier_init(&barrierBlurPass1,  NULL, cfg.num_threads + 1);
        pthread_barrier_init(&barrierDoFPass1,   NULL, cfg.num_threads + 1);
        pthread_barrier_init(&barrierDoFPass2,   NULL, cfg.num_threads + 1);
        pthread_barrier_init(&barrierTilesDone,  NULL, cfg.num_threads + 1);

        initThreads(&ctx);
    }

    if (cfg.zbuffer)
        zbuffer = malloc(cfg.screen_width * cfg.screen_height * sizeof(float));

    // ── Boucle principale ─────────────────────────────────────────────────────
    while (!WindowShouldClose())
    {
        ctx.cameraPos = camera.position;

        rotX += 0.01f; rotY -= 0.015f; rotZ += 0.015f;
        Matrix rotation = MatrixRotateXYZ((Vector3){ rotX, rotY, rotZ });

        if (ctx.envMap.data && cfg.tiles) {
            Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
            Vector3 right   = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
            Vector3 up      = Vector3CrossProduct(right, forward);
            ctx.cameraForward = forward;
            ctx.cameraRight   = right;
            ctx.cameraUp      = up;

            static Vector3 lastForward = {0};
            if (fabsf(forward.x-lastForward.x) > 0.0001f ||
                fabsf(forward.y-lastForward.y) > 0.0001f ||
                fabsf(forward.z-lastForward.z) > 0.0001f) {
                PrecomputeSkyboxLUT(&ctx);
                lastForward = forward;
            }
        }

        BeginDrawing();
        ClearBackground(BLACK);

        Matrix proj    = buildProjectionMatrix(&ctx);
        Matrix view    = GetCameraMatrix(camera);
        Matrix vp      = MatrixMultiply(view, proj);
        Frustum frustum = extractFrustum(vp);

        // ── Préparation des polygones ─────────────────────────────────────────
        int inc = 0;
        for (int i = 0; i < mesh.triangleCount; i++) {
            //int i0 = i*3, i1 = i*3+1, i2 = i*3+2;

            // ON RÉCUPÈRE LES INDICES DEPUIS LE BUFFER D'INDICES
            int i0 = ((unsigned short *)mesh.indices)[i * 3 + 0];
            int i1 = ((unsigned short *)mesh.indices)[i * 3 + 1];
            int i2 = ((unsigned short *)mesh.indices)[i * 3 + 2];

            Vector3 v0 = Vector3Transform(getVertex(mesh, i0), rotation);
            Vector3 v1 = Vector3Transform(getVertex(mesh, i1), rotation);
            Vector3 v2 = Vector3Transform(getVertex(mesh, i2), rotation);

            if (cfg.backface_culling && isBackFace(v0, v1, v2, ctx.cameraPos)) continue;

            if (cfg.frustum_culling) {
                Vector3 aabbMin = { fminf(v0.x,fminf(v1.x,v2.x)), fminf(v0.y,fminf(v1.y,v2.y)), fminf(v0.z,fminf(v1.z,v2.z)) };
                Vector3 aabbMax = { fmaxf(v0.x,fmaxf(v1.x,v2.x)), fmaxf(v0.y,fmaxf(v1.y,v2.y)), fmaxf(v0.z,fmaxf(v1.z,v2.z)) };
                if (!aabbInFrustum(&frustum, aabbMin, aabbMax)) continue;
                if (!triangleInFrustum(&frustum, v0, v1, v2))  continue;
            }

            Poly* p = &visiblePolys[inc];

            p->p0 = GetWorldToScreen(v0, camera);
            p->p1 = GetWorldToScreen(v1, camera);
            p->p2 = GetWorldToScreen(v2, camera);

            Vector3 cv0 = Vector3Transform(v0, view);
            Vector3 cv1 = Vector3Transform(v1, view);
            Vector3 cv2 = Vector3Transform(v2, view);
            p->z0 = cv0.z; p->z1 = cv1.z; p->z2 = cv2.z;
            p->zmin = fminf(p->z0, fminf(p->z1, p->z2));
            p->zmax = fmaxf(p->z0, fmaxf(p->z1, p->z2));

            float minX = fminf(p->p0.x, fminf(p->p1.x, p->p2.x));
            float maxX = fmaxf(p->p0.x, fmaxf(p->p1.x, p->p2.x));
            float minY = fminf(p->p0.y, fminf(p->p1.y, p->p2.y));
            float maxY = fmaxf(p->p0.y, fmaxf(p->p1.y, p->p2.y));

            p->minX = fmaxf(0.0f, minX);
            p->maxX = fminf(ctx.screenWidth-1,  maxX);
            p->minY = fmaxf(0.0f, minY);
            p->maxY = fminf(ctx.screenHeight-1, maxY);

            if (p->maxX < p->minX || p->maxY < p->minY) continue;

            p->tileMinX = (int)(p->minX) / ctx.tile_size;
            p->tileMaxX = (int)(p->maxX) / ctx.tile_size;
            p->tileMinY = (int)(p->minY) / ctx.tile_size;
            p->tileMaxY = (int)(p->maxY) / ctx.tile_size;

            p->uv0 = getUV(mesh, i0);
            p->uv1 = getUV(mesh, i1);
            p->uv2 = getUV(mesh, i2);

            p->v0 = v0; p->v1 = v1; p->v2 = v2;

            Vector3 edge1  = Vector3Subtract(v1, v0);
            Vector3 edge2  = Vector3Subtract(v2, v0);
            float duv1x = p->uv1.x-p->uv0.x, duv1y = p->uv1.y-p->uv0.y;
            float duv2x = p->uv2.x-p->uv0.x, duv2y = p->uv2.y-p->uv0.y;
            float denom = duv1x*duv2y - duv2x*duv1y;

            if (fabsf(denom) < 1e-6f) {
                p->tangent   = (Vector3){1,0,0};
                p->bitangent = (Vector3){0,1,0};
            } else {
                float f = 1.0f / denom;
                p->tangent   = Vector3Normalize((Vector3){
                    f*(duv2y*edge1.x - duv1y*edge2.x),
                    f*(duv2y*edge1.y - duv1y*edge2.y),
                    f*(duv2y*edge1.z - duv1y*edge2.z)});
                p->bitangent = Vector3Normalize((Vector3){
                    f*(-duv2x*edge1.x + duv1x*edge2.x),
                    f*(-duv2x*edge1.y + duv1x*edge2.y),
                    f*(-duv2x*edge1.z + duv1x*edge2.z)});
            }

            p->n0 = Vector3Normalize(Vector3Transform(getNormal(mesh, i0, smoothNormals), rotation));
            p->n1 = Vector3Normalize(Vector3Transform(getNormal(mesh, i1, smoothNormals), rotation));
            p->n2 = Vector3Normalize(Vector3Transform(getNormal(mesh, i2, smoothNormals), rotation));

            p->tangent   = Vector3Normalize(Vector3Transform(p->tangent,   rotation));
            p->bitangent = Vector3Normalize(Vector3Transform(p->bitangent, rotation));

            p->couleur = PolyList[i].couleur;
            if (cfg.zbuffer) gouraudShading(&ctx, p);
            if (cfg.warnock) flatShading(&ctx, p, view);

            p->visible = true;
            inc++;
        }

        int polyCount  = inc;
        ctx.polys      = visiblePolys;
        ctx.polyCount  = polyCount;

        // ── Stats ─────────────────────────────────────────────────────────────
        static int displayTotal = 0, displayRendus = 0, displayRejetes = 0;
        static int frameCounter = 0;
        frameCounter++;
        if (frameCounter % 2 == 0) {
            displayTotal   = mesh.triangleCount;
            displayRendus  = polyCount;
            displayRejetes = mesh.triangleCount - polyCount;
        }

        // ── Contrôles caméra ──────────────────────────────────────────────────
        if (IsKeyDown(KEY_UP))    camera.position.y += 1.0f;
        if (IsKeyDown(KEY_DOWN))  camera.position.y -= 1.0f;
        if (IsKeyDown(KEY_RIGHT)) camera.position.x += 1.0f;
        if (IsKeyDown(KEY_LEFT))  camera.position.x -= 1.0f;
        if (IsKeyDown(KEY_W))     camera.position.z -= 1.0f;
        if (IsKeyDown(KEY_S))     camera.position.z += 1.0f;
        if (IsKeyDown(KEY_V))     camera.target.y   += 1.0f;
        if (IsKeyDown(KEY_T))     camera.target.y   -= 1.0f;
        if (IsKeyDown(KEY_F))     camera.target.x   += 1.0f;
        if (IsKeyDown(KEY_H))     camera.target.x   -= 1.0f;
        if (IsKeyDown(KEY_P))     camera.target.z   += 1.0f;
        if (IsKeyDown(KEY_L))     camera.target.z   -= 1.0f;

        // ── Contrôles DoF ─────────────────────────────────────────────────────
        if (IsKeyDown(KEY_KP_ADD))      ctx.focalDistance -= 0.1f;
        if (IsKeyDown(KEY_KP_SUBTRACT)) ctx.focalDistance += 0.1f;
        if (IsKeyDown(KEY_KP_MULTIPLY)) ctx.focalRange    += 0.1f;
        if (IsKeyDown(KEY_KP_DIVIDE))   ctx.focalRange     = fmaxf(0.1f, ctx.focalRange - 0.1f);
        if (IsKeyDown(KEY_KP_0))        ctx.maxBlurRadius  = Clamp(ctx.maxBlurRadius + 1, 1, 20);
        if (IsKeyDown(KEY_KP_1))        ctx.maxBlurRadius  = Clamp(ctx.maxBlurRadius - 1, 1, 20);

        // ── Rendu ─────────────────────────────────────────────────────────────
        if (cfg.warnock) {
            DrawText(TextFormat("Warnock + ZBuffer, depth=%d", cfg.tree_depth), 10, 10, 20, WHITE);
            Region root = {0, 0, cfg.screen_width, cfg.screen_height};
            int indices[cfg.max_poly];
            for (int i = 0; i < polyCount; i++) indices[i] = i;
            ctx.rootIndices = indices;
            warnock(&ctx, &root, indices, polyCount, 0);
        }

        if (cfg.zbuffer) {
            DrawText("ZBuffer", 10, 10, 20, WHITE);
            for (int i = 0; i < cfg.screen_width * cfg.screen_height; i++)
                zbuffer[i] = 1e9f;
            for (int i = 0; i < polyCount; i++)
                drawTriangleZ(&ctx, &ctx.polys[i], zbuffer);
        }

        if (cfg.tiles) {
            qsort(ctx.polys, ctx.polyCount, sizeof(Poly), compare_zmin);
            for (int i = 0; i < tilesX * tilesY; i++) {
                tiles[i].minZ = 1e9f;
                tiles[i].maxZ = -1e9f;
            }
            binTriangles(&ctx);
            renderFrame(&ctx);
            UpdateTexture(tex, framebuffer);
            DrawTexture(tex, 0, 0, WHITE);

            if (cfg.debug_tiles) {
                for (int ty = 0; ty < tilesY; ty++) {
                    for (int tx = 0; tx < tilesX; tx++) {
                        Tile* tile = &tiles[ty * tilesX + tx];
                        int x = tx * cfg.tile_size;
                        int y = ty * cfg.tile_size;
                        int c = tile->count + 17;
                        unsigned char alpha = (unsigned char)Clamp(c * 10, 20, 200);
                        Color col       = { (unsigned char)(c*5), 0, 0, alpha };
                        Color whiteText = { (unsigned char)(c*5), (unsigned char)(c*5), (unsigned char)(c*5), alpha };
                        DrawRectangleLinesEx((Rectangle){x, cfg.screen_height-(y+cfg.tile_size), cfg.tile_size, cfg.tile_size}, 1.5f, col);
                        DrawText(TextFormat("%d", tile->count), x+2, cfg.screen_height-y-14, 10, whiteText);
                    }
                }
            }
            DrawText("Tiles", 10, 10, 20, WHITE);
        }

        // ── HUD ───────────────────────────────────────────────────────────────
        DrawText(TextFormat("FPS = %d", GetFPS()), 10, 40, 20, WHITE);
        DrawText(TextFormat("Triangles total  : %d", displayTotal),   10,  70, 20, WHITE);
        DrawText(TextFormat("Triangles rendus : %d", displayRendus),  10,  95, 20, WHITE);
        DrawText(TextFormat("Triangles rejetés: %d", displayRejetes), 10, 120, 20, WHITE);
        DrawText(TextFormat("pos: %.1f %.1f %.1f", camera.position.x, camera.position.y, camera.position.z), 10, 150, 20, WHITE);
        DrawText(TextFormat("Target: %.1f %.1f %.1f", camera.target.x, camera.target.y, camera.target.z),    10, 180, 20, WHITE);
        if (cfg.dof && cfg.tiles)
            DrawText(TextFormat("DoF focal: %.1f  range: %.1f  blur: %d",
                ctx.focalDistance, ctx.focalRange, ctx.maxBlurRadius), 10, 210, 20, YELLOW);

        EndDrawing();
    }

    // ── Nettoyage ─────────────────────────────────────────────────────────────
    if (cfg.tiles){
        free(threads);
        for (int i = 0; i < cfg.num_threads; i++) {
            free(threadsData[i].r_sum);
            free(threadsData[i].g_sum);
            free(threadsData[i].b_sum);
        }
        free(threadsData);

        free(framebuffer);
        free(framebufferBlur);
        free(depthBuffer);

        free(all_indices);
        free(tiles);

        if (ctx.skyU)       free(ctx.skyU);
        if (ctx.skyV)       free(ctx.skyV);
    }

    free(smoothNormals);
    free(PolyList);
    free(visiblePolys);

    if (cfg.zbuffer)    free(zbuffer);

    UnloadModel(model);
    if (ctx.texImage.data)  UnloadImage(ctx.texImage);
    if (ctx.normalMap.data) UnloadImage(ctx.normalMap);
    if (ctx.envMap.data)    UnloadImage(ctx.envMap);

    CloseWindow();
    return 0;
}
