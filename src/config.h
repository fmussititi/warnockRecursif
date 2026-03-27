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
  
    // DepthOfField
    int dof;
    float focalDistance, focalRange;
    int   maxBlurRadius;

    // Tiles
    int num_threads;
    int tile_size;
    int max_tri_per_tile;
    int max_tiles;
    int debug_tiles;

    // Warnock
    int tree_depth;
    int contour_arbre;
    int max_poly;

} Config;

Config loadConfig(const char* filename);