#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    // Rendu
    int warnock;
    int zbuffer;
    int tiles;

    // Resolution
    int screen_width;
    int screen_height;

    // Modele
    char model_path[256];

    // Camera
    float cam_x, cam_y, cam_z;
    float target_x, target_y, target_z;
    float fov, near, far;

    // Culling
    int frustum_culling;
    int backface_culling;

    // Lumiere
    float light_x, light_y, light_z;
    float ambient, diffuse, specular, shininess;
    float refl1, refl2;

    // Textures
    int textures_enabled;
    char tex_diffuse[256];
    char tex_normal[256];

    // EnvironementMap
    int envMap_enable;
    char envMap[256];

    // SkyBox
    int blur;
    int radius;
    int pass;

    // Tiles
    int num_threads;
    int tile_size;
    int max_tri_per_tile;
    int debug_tiles;

    // Warnock
    int tree_depth;
    int contour_arbre;
    int max_poly;

} Config;

// Parser simple clé=valeur
Config loadConfig(const char* filename)
{
    // Valeurs par défaut
    Config cfg = {
        .warnock        = 0,
        .zbuffer        = 0,
        .tiles          = 1,
        .screen_width   = 1600,
        .screen_height  = 900,
        .model_path     = "./ressources/susaneHiDef.obj",
        .cam_x          = 5.0f, .cam_y = 5.0f, .cam_z = 5.0f,
        .target_x       = 0.0f, .target_y = 0.0f, .target_z = 0.0f,
        .fov            = 25.0f, .near = 0.01f, .far = 1000.0f,
        .frustum_culling = 1,
        .backface_culling = 1,
        .light_x        = 1.0f, .light_y = 1.0f, .light_z = -1.0f,
        .ambient  = 0.2f, .diffuse = 0.6f,
        .specular = 1.5f, .shininess = 64.0f,
        .refl1 = 0.15f,   .refl2 = 0.1f,
        .textures_enabled = 1,
        .tex_diffuse    = "./ressources/rusty_metal_02_diff_1k.jpg",
        .tex_normal     = "./ressources/rusty_metal_02_nor_gl_1k.jpg",
        .envMap_enable  = 1,
        .envMap         = "./ressources/bell_tower.jpg",
        .blur           = 1,
        .radius         = 5,
        .pass           = 3,
        .num_threads    = 8,
        .tile_size      = 32,
        .max_tri_per_tile = 20000,
        .debug_tiles    = 0,
        .tree_depth     = 10,
        .contour_arbre  = 1,
        .max_poly       = 10000
    };

    FILE* f = fopen(filename, "r");
    if (!f) {
        printf("config.ini non trouvé, valeurs par défaut utilisées\n");
        return cfg;
    }

    char line[512];
    while (fgets(line, sizeof(line), f))
    {
        // Ignorer commentaires et sections
        if (line[0] == '#' || line[0] == ';' || line[0] == '[' || line[0] == '\n')
            continue;

        char key[128], value[256];
        if (sscanf(line, "%127[^=]=%255s", key, value) != 2)
            continue;

        // Trim espaces
        char* k = key;
        while (*k == ' ') k++;
        char* v = value;
        while (*v == ' ') v++;

        // Parser les clés
        if      (!strcmp(k, "warnock"))           cfg.warnock           = atoi(v);
        else if (!strcmp(k, "zbuffer"))           cfg.zbuffer           = atoi(v);
        else if (!strcmp(k, "tiles"))             cfg.tiles             = atoi(v);
        else if (!strcmp(k, "screen_width"))      cfg.screen_width      = atoi(v);
        else if (!strcmp(k, "screen_height"))     cfg.screen_height     = atoi(v);
        else if (!strcmp(k, "model_path"))        strncpy(cfg.model_path, v, 255);
        else if (!strcmp(k, "position_x"))        cfg.cam_x             = atof(v);
        else if (!strcmp(k, "position_y"))        cfg.cam_y             = atof(v);
        else if (!strcmp(k, "position_z"))        cfg.cam_z             = atof(v);
        else if (!strcmp(k, "target_x"))          cfg.target_x          = atof(v);
        else if (!strcmp(k, "target_y"))          cfg.target_y          = atof(v);
        else if (!strcmp(k, "target_z"))          cfg.target_z          = atof(v);
        else if (!strcmp(k, "fov"))               cfg.fov               = atof(v);
        else if (!strcmp(k, "near"))              cfg.near              = atof(v);
        else if (!strcmp(k, "far"))               cfg.far               = atof(v);
        else if (!strcmp(k, "frustum_culling"))   cfg.frustum_culling   = atoi(v);
        else if (!strcmp(k, "backface_culling"))  cfg.backface_culling  = atoi(v);
        else if (!strcmp(k, "dir_x"))             cfg.light_x           = atof(v);
        else if (!strcmp(k, "dir_y"))             cfg.light_y           = atof(v);
        else if (!strcmp(k, "dir_z"))             cfg.light_z           = atof(v);
        else if (!strcmp(k, "ambient_light"))     cfg.ambient           = atof(v);
        else if (!strcmp(k, "diffuse_light"))     cfg.diffuse           = atof(v);
        else if (!strcmp(k, "specular_light"))    cfg.specular          = atof(v);
        else if (!strcmp(k, "shininess_light"))   cfg.shininess         = atof(v);
        else if (!strcmp(k, "refl1"))             cfg.refl1             = atof(v);
        else if (!strcmp(k, "refl2"))             cfg.refl2             = atof(v);
        else if (!strcmp(k, "enabled"))           cfg.textures_enabled  = atoi(v);
        else if (!strcmp(k, "diffuse_tex"))       strncpy(cfg.tex_diffuse, v, 255);
        else if (!strcmp(k, "normal_tex"))        strncpy(cfg.tex_normal,  v, 255);
        else if (!strcmp(k, "enableEnvMap"))      cfg.envMap_enable     = atoi(v);
        else if (!strcmp(k, "envMap"))            strncpy(cfg.envMap,      v, 255);
        else if (!strcmp(k, "blur"))              cfg.blur              = atoi(v);
        else if (!strcmp(k, "radius"))            cfg.radius            = atoi(v);
        else if (!strcmp(k, "pass"))              cfg.pass              = atoi(v);
        else if (!strcmp(k, "num_threads"))       cfg.num_threads       = atoi(v);
        else if (!strcmp(k, "tile_size"))         cfg.tile_size         = atoi(v);
        else if (!strcmp(k, "max_tri_per_tile"))  cfg.max_tri_per_tile  = atoi(v);
        else if (!strcmp(k, "debug_tiles"))       cfg.debug_tiles       = atoi(v);
        else if (!strcmp(k, "tree_depth"))        cfg.tree_depth        = atoi(v);
        else if (!strcmp(k, "contour_arbre"))     cfg.contour_arbre        = atoi(v);
        else if (!strcmp(k, "max_poly"))          cfg.max_poly          = atoi(v);
    }

    fclose(f);
    printf("Config chargée depuis %s\n", filename);
    return cfg;
}