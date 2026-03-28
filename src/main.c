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
#include "painter.h"
#include "warnock.h"
#include "zbuffer.h"
#include "tiles.h"
#include "frustum.h"

int main(void)
{
    Config cfg = loadConfig("config.ini");

    const char* mode = "Unknown";
    if      (cfg.painter) mode = "Painter";
    else if (cfg.warnock) mode = "Warnock";
    else if (cfg.zbuffer) mode = "ZBuffer";
    else if (cfg.tiles)   mode = TextFormat("Tiles - %d threads", cfg.num_threads);
    else return 1;

    InitWindow(cfg.screen_width, cfg.screen_height,
        TextFormat("SoftRender3D - %s %dx%d", mode, cfg.screen_width, cfg.screen_height));

    framebuffer = malloc(cfg.screen_width * cfg.screen_height * sizeof(Color));
        if (!framebuffer) { printf("ERREUR: malloc framebuffer failed!\n"); return 1; }

    if (cfg.tiles){

        tilesX = cfg.screen_width  / cfg.tile_size;
        tilesY = cfg.screen_height / cfg.tile_size;

        framebufferBlur = malloc(cfg.screen_width * cfg.screen_height * sizeof(Color));
        if (!framebufferBlur) { printf("ERREUR: malloc framebufferBlur failed!\n"); return 1; }

        // 1. Calculer le nombre de tuiles nécessaires
        int _tilesX = (cfg.screen_width  + cfg.tile_size - 1) / cfg.tile_size;
        int _tilesY = (cfg.screen_height + cfg.tile_size - 1) / cfg.tile_size;
        int total_tiles = _tilesX * _tilesY;

        // 2. Mettre à jour la config pour que le reste du code soit cohérent
        cfg.max_tiles = total_tiles; 

        // 3. Allocation principale
        tiles = malloc(cfg.max_tiles * sizeof(Tile));
        if (!tiles) { 
            fprintf(stderr, "ERREUR: malloc %d tiles failed!\n", cfg.max_tiles); 
            return 1; 
        }

        // Alloue TOUS les indices d'un coup
        all_indices = malloc(cfg.max_tiles * cfg.max_tri_per_tile * sizeof(int));
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

    if (cfg.zbuffer || cfg.tiles)
        zbuffer = malloc(cfg.screen_width * cfg.screen_height * sizeof(float));

    if (cfg.warnock)
        cfg.backface_culling = 0;

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
    ctx.hybride       = cfg.hybride;
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
    ctx.flatShading   = cfg.flatShading;
    ctx.gouraudShading= cfg.gouraudShading;

    
    // ── Smooth normals ────────────────────────────────────────────────────────
    Vector3* smoothNormals    = calloc(vertexCount, sizeof(Vector3));
    CachedVertex* vertexCache = malloc(mesh.vertexCount * sizeof(CachedVertex));
    Poly*    PolyList         = malloc(mesh.vertexCount/3 * sizeof(Poly));
    Poly*    visiblePolys     = malloc(mesh.triangleCount * sizeof(Poly)); 
    Image    img;
    Texture2D tex;

    img = GenImageColor(cfg.screen_width, cfg.screen_height, RAYWHITE);
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);  // ← forcer RGBA
    tex = LoadTextureFromImage(img);
    UnloadImage(img);

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

    // ── Précalcul tangentes/bitangentes en espace objet ──────────────────────
    Vector3* tangentsOS   = malloc(mesh.triangleCount * sizeof(Vector3));
    Vector3* bitangentsOS = malloc(mesh.triangleCount * sizeof(Vector3));

    for (int i = 0; i < mesh.triangleCount; i++) {
        int idx0 = ((unsigned short*)mesh.indices)[i*3+0];
        int idx1 = ((unsigned short*)mesh.indices)[i*3+1];
        int idx2 = ((unsigned short*)mesh.indices)[i*3+2];

        Vector3 v0 = getVertex(mesh, idx0);
        Vector3 v1 = getVertex(mesh, idx1);
        Vector3 v2 = getVertex(mesh, idx2);
        Vector2 uv0 = getUV(mesh, idx0);
        Vector2 uv1 = getUV(mesh, idx1);
        Vector2 uv2 = getUV(mesh, idx2);

        Vector3 edge1  = Vector3Subtract(v1, v0);
        Vector3 edge2  = Vector3Subtract(v2, v0);
        float duv1x = uv1.x-uv0.x, duv1y = uv1.y-uv0.y;
        float duv2x = uv2.x-uv0.x, duv2y = uv2.y-uv0.y;
        float denom = duv1x*duv2y - duv2x*duv1y;

        if (fabsf(denom) < 1e-6f) {
            tangentsOS[i]   = (Vector3){1, 0, 0};
            bitangentsOS[i] = (Vector3){0, 1, 0};
        } else {
            float f = 1.0f / denom;
            tangentsOS[i] = Vector3Normalize((Vector3){
                f*(duv2y*edge1.x - duv1y*edge2.x),
                f*(duv2y*edge1.y - duv1y*edge2.y),
                f*(duv2y*edge1.z - duv1y*edge2.z)});
            bitangentsOS[i] = Vector3Normalize((Vector3){
                f*(-duv2x*edge1.x + duv1x*edge2.x),
                f*(-duv2x*edge1.y + duv1x*edge2.y),
                f*(-duv2x*edge1.z + duv1x*edge2.z)});
        }
    }
    printf("Tangentes précalculées : %d triangles\n", mesh.triangleCount);


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

        pthread_barrier_init(&barrierStart,      NULL, cfg.num_threads + 1);
        pthread_barrier_init(&barrierEnd,        NULL, cfg.num_threads + 1);
        pthread_barrier_init(&barrierSkyboxDone, NULL, cfg.num_threads + 1);
        pthread_barrier_init(&barrierDoFPass1,   NULL, cfg.num_threads + 1);
        pthread_barrier_init(&barrierTilesDone,  NULL, cfg.num_threads + 1);

        initThreads(&ctx);
    }


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

        for (int i = 0; i < mesh.vertexCount; i++) {
            vertexCache[i].isProjected = false;}

        // ── Préparation des polygones ─────────────────────────────────────────
        int inc = 0;
        for (int i = 0; i < mesh.triangleCount; i++) {

            // 1. Récupération des indices uniques
            int idx[3];
            idx[0] = ((unsigned short *)mesh.indices)[i * 3 + 0];
            idx[1] = ((unsigned short *)mesh.indices)[i * 3 + 1];
            idx[2] = ((unsigned short *)mesh.indices)[i * 3 + 2];

            CachedVertex* v[3];

            // 2. Vertex Shader (Cache)
            for (int j = 0; j < 3; j++) {
                v[j] = &vertexCache[idx[j]];
                if (!v[j]->isProjected) {
                    Vector3 rawPos = getVertex(mesh, idx[j]);
                    v[j]->worldPos  = Vector3Transform(rawPos, rotation);
                    v[j]->viewPos   = Vector3Transform(v[j]->worldPos, view);
                    v[j]->screenPos = GetWorldToScreen(v[j]->worldPos, camera);
                    // On transforme la normale lissée une seule fois
                    v[j]->normal    = Vector3Normalize(Vector3Transform(smoothNormals[idx[j]], rotation));
                    v[j]->isProjected = true;
                }
            }

            // Raccourcis pour la lisibilité
            Vector3 v0 = v[0]->worldPos;
            Vector3 v1 = v[1]->worldPos;
            Vector3 v2 = v[2]->worldPos;

            // 3. Cullings (utilisent les positions monde du cache)
            if (cfg.backface_culling && isBackFace(v0, v1, v2, ctx.cameraPos)) continue;

            if (cfg.frustum_culling) {
                Vector3 aabbMin = { fminf(v0.x,fminf(v1.x,v2.x)), fminf(v0.y,fminf(v1.y,v2.y)), fminf(v0.z,fminf(v1.z,v2.z)) };
                Vector3 aabbMax = { fmaxf(v0.x,fmaxf(v1.x,v2.x)), fmaxf(v0.y,fmaxf(v1.y,v2.y)), fmaxf(v0.z,fmaxf(v1.z,v2.z)) };
                if (!aabbInFrustum(&frustum, aabbMin, aabbMax)) continue;
                if (!triangleInFrustum(&frustum, v0, v1, v2)) continue;
            }

            // 4. Préparation du polygone visible
            Poly* p = &visiblePolys[inc];

            // On récupère les positions écran et Z directement du cache
            p->p0 = v[0]->screenPos;
            p->p1 = v[1]->screenPos;
            p->p2 = v[2]->screenPos;

            p->z0 = v[0]->viewPos.z; 
            p->z1 = v[1]->viewPos.z; 
            p->z2 = v[2]->viewPos.z;
            p->zmin = fminf(p->z0, fminf(p->z1, p->z2));
            p->zmax = fmaxf(p->z0, fmaxf(p->z1, p->z2));

            // Bounding box pour le tiling
            float minX = fminf(p->p0.x, fminf(p->p1.x, p->p2.x));
            float maxX = fmaxf(p->p0.x, fmaxf(p->p1.x, p->p2.x));
            float minY = fminf(p->p0.y, fminf(p->p1.y, p->p2.y));
            float maxY = fmaxf(p->p0.y, fmaxf(p->p1.y, p->p2.y));

            p->minX = fmaxf(0.0f, minX);
            p->maxX = fminf(ctx.screenWidth-1,  maxX);
            p->minY = fmaxf(0.0f, minY);
            p->maxY = fminf(ctx.screenHeight-1, maxY);

            if (p->maxX < p->minX || p->maxY < p->minY) continue;

            // Calcul des tuiles
            p->tileMinX = (int)(p->minX) / ctx.tile_size;
            p->tileMaxX = (int)(p->maxX) / ctx.tile_size;
            p->tileMinY = (int)(p->minY) / ctx.tile_size;
            p->tileMaxY = (int)(p->maxY) / ctx.tile_size;

            // UVs (utilisent les index uniques)
            p->uv0 = getUV(mesh, idx[0]);
            p->uv1 = getUV(mesh, idx[1]);
            p->uv2 = getUV(mesh, idx[2]);

            p->v0 = v0; p->v1 = v1; p->v2 = v2;

            // 5. Normales lissées (Directement du cache !)
            p->n0 = v[0]->normal;
            p->n1 = v[1]->normal;
            p->n2 = v[2]->normal;

            p->tangent   = Vector3Normalize(Vector3Transform(tangentsOS[i],   rotation));
            p->bitangent = Vector3Normalize(Vector3Transform(bitangentsOS[i], rotation));

            p->couleur = PolyList[i].couleur;           
            if (cfg.warnock){ 
                if (cfg.flatShading) flatShading(&ctx, p, view);
            }
            if (cfg.painter || cfg.zbuffer){
                if (cfg.gouraudShading)   gouraudShading(&ctx, p);
                else if (cfg.flatShading) flatShading(&ctx, p, view);
            }

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

        if (IsKeyPressed(KEY_F1)) {
            static void* savedTexData    = NULL;
            static void* savedNormalData = NULL;

            cfg.textures_enabled = !cfg.textures_enabled;

            if (!cfg.textures_enabled) {
                // Sauvegarde et désactivation
                savedTexData    = ctx.texImage.data;
                savedNormalData = ctx.normalMap.data;
                ctx.texImage.data  = NULL;
                ctx.normalMap.data = NULL;
            } else {
                // Restauration
                ctx.texImage.data  = savedTexData;
                ctx.normalMap.data = savedNormalData;
            }
        }

        if (IsKeyPressed(KEY_F2)) {
            static void* savedEnvData = NULL;

            ctx.envMap_enable = !ctx.envMap_enable;

            if (!ctx.envMap_enable) {
                savedEnvData    = ctx.envMap.data;
                ctx.envMap.data = NULL;
            } else {
                ctx.envMap.data = savedEnvData;
                PrecomputeSkyboxLUT(&ctx);  // recalcul LUT au réactivation
            }
        }

        if (IsKeyPressed(KEY_F3)) ctx.dof = !ctx.dof;

        // ── Rendu ─────────────────────────────────────────────────────────────
        if (cfg.painter) {
            clear_framebuffer(&ctx, (Color){ 20, 20, 30, 255 });            
            drawPainter(&ctx);
            UpdateTexture(tex, framebuffer);
            DrawTexture(tex, 0, 0, WHITE);
            DrawText(TextFormat("Painter"), 10, 10, 20, WHITE);
        }

        if (cfg.warnock) {
            Region root = {0, 0, cfg.screen_width, cfg.screen_height};
            int indices[cfg.max_poly];
            for (int i = 0; i < polyCount; i++) indices[i] = i;
            ctx.rootIndices = indices;

            clear_framebuffer(&ctx, (Color){ 20, 20, 30, 255 }); 
            warnock(&ctx, &root, indices, polyCount, 0);
            UpdateTexture(tex, framebuffer);
            DrawTexture(tex, 0, 0, WHITE);
            DrawText(TextFormat("Warnock, depth=%d", cfg.tree_depth), 10, 10, 20, WHITE);
        }

        if (cfg.zbuffer) {            
            for (int i = 0; i < cfg.screen_width * cfg.screen_height; i++)
                zbuffer[i] = 1e9f;
            clear_framebuffer(&ctx, (Color){ 20, 20, 30, 255 }); 
            for (int i = 0; i < polyCount; i++)
                drawTriangleZ(&ctx, &ctx.polys[i], zbuffer);

            UpdateTexture(tex, framebuffer);
            DrawTexture(tex, 0, 0, WHITE);
            DrawText("ZBuffer", 10, 10, 20, WHITE);
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
        if (cfg.tiles)
            DrawText(TextFormat("F1 Textures: %s  F2 EnvMap: %s  F3 DoF: %s",
                ctx.texImage.data  ? "ON" : "OFF",
                ctx.envMap.data    ? "ON" : "OFF",
                ctx.dof            ? "ON" : "OFF"),
                10, 210, 20, LIGHTGRAY);
        if (ctx.dof && cfg.tiles)
            DrawText(TextFormat("DoF focal: %.1f  range: %.1f  blur: %d",
                ctx.focalDistance, ctx.focalRange, ctx.maxBlurRadius), 10, 240, 20, YELLOW);

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

        free(framebufferBlur);
        free(all_indices);
        free(tiles);

        if (ctx.skyU) free(ctx.skyU);
        if (ctx.skyV) free(ctx.skyV);
    }

    if (cfg.zbuffer || cfg.tiles) free(zbuffer);
    
    free(framebuffer);
    free(smoothNormals);
    free(tangentsOS);
    free(bitangentsOS);
    free(vertexCache);
    free(PolyList);
    free(visiblePolys);

    UnloadModel(model);
    if (ctx.texImage.data)  UnloadImage(ctx.texImage);
    if (ctx.normalMap.data) UnloadImage(ctx.normalMap);
    if (ctx.envMap.data)    UnloadImage(ctx.envMap);

    CloseWindow();
    return 0;
}
