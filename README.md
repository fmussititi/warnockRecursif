# SoftRender3D

Un moteur de rendu 3D software implémenté entièrement sur CPU en C, avec Raylib pour la fenêtre et l'affichage.

> Projet pédagogique visant à comprendre les algorithmes de rendu 3D from scratch, sans GPU.

---

## Fonctionnalités

### Algorithmes de rendu
- **Warnock** — algorithme récursif de subdivision de régions avec Z-buffer hybride
- **Z-Buffer** — rendu classique pixel par pixel avec test de profondeur
- **Tiles** — rendu par tuiles avec multithreading (taille configurable)

### Éclairage & Shading
- **Flat shading** — une couleur par triangle
- **Gouraud shading** — interpolation des couleurs par sommet
- **Phong shading** — interpolation des normales par pixel
- **Blinn-Phong specular** — reflet brillant configurable (`shininess`, `specular`)
- **Smooth normals** — calcul automatique par fusion des normales voisines en 2 passes

### Textures & Matériaux
- **Texture mapping** — interpolation des coordonnées UV par pixel
- **Normal mapping** — perturbation des normales via texture (espace tangent, matrice TBN)
- **Environment mapping** — réflexions via skybox équirectangulaire
- **Fresnel** — intensité de réflexion variable selon l'angle de vue
- Support des formats JPG et PNG

### Skybox
- Projection équirectangulaire avec LUT précalculée par frame
- Rendu parallélisé par tranches de lignes

### Post-processing
- **Depth of Field (DoF)** — flou de profondeur en 3 passes hexagonales
  - Pass 1 : horizontale
  - Pass 2 : diagonale 60°
  - Pass 3 : diagonale -60°
  - Résultat : noyau hexagonal (bokeh cinématographique)
- Paramètres ajustables en temps réel au clavier

### Performance
- **Multithreading** — N threads POSIX configurables, synchronisation par barrières (`pthread_barrier`)
- **SIMD AVX2** — rasterisation 8 pixels par cycle via intrinsics `__m256`
- **Tile binning** — assignation des triangles aux tuiles via AABB + test d'intersection précis
- **Z-buffer local** — un Z-buffer par tuile pour éviter les conflits entre threads
- **Depth buffer global** — pour le DoF, écrit en même temps que le framebuffer
- **Frustum culling** — 6 plans de clipping extraits de la matrice ViewProjection
- **Backface culling** — optionnel

---

## Architecture des fichiers

```
src/
├── main.c          — boucle principale, pipeline CPU, projection, contrôles
├── globals.c/h     — variables globales (framebuffer, depthBuffer, tiles, barrières)
├── utils.c/h       — helpers (getVertex, getUV, getNormal, compare_zmin...)
├── skybox.c/h      — LUT équirectangulaire, SampleEquirectangular
├── shading.c/h     — flatShading, gouraudShading
├── warnock.c/h     — algorithme Warnock + drawRegionZBuffer
├── zbuffer.c/h     — drawTriangleZ (mode Z-buffer classique)
├── frustum.c/h     — extractFrustum, aabbInFrustum, triangleInFrustum
├── tiles.c/h       — binTriangles, drawTile, worker, initThreads, renderFrame
└── config.c/h      — chargement config.ini

include/
└── main.h          — structs Poly, Region, RenderContext, ThreadData
```

---

## Configuration

Tout est configurable via `config.ini`, sans recompiler :

```ini
[window]
screen_width=1536
screen_height=900

[camera]
cam_x=10 ; cam_y=10 ; cam_z=10
target_x=0 ; target_y=0 ; target_z=0
fov=60

[render]
tiles=1
warnock=0
zbuffer=0
tile_size=32
num_threads=8
max_tri_per_tile=10000
max_tiles=2000
backface_culling=1
frustum_culling=1

[lighting]
light_x=1 ; light_y=1 ; light_z=1
ambient=0.2
diffuse=0.8
specular=0.5
shininess=120

[textures]
enabled=1
diffuse_tex=./ressources/diffuse.png
normal_tex=./ressources/normal.png

[environmentMap]
enableEnvMap=1
envMap=./ressources/skybox.jpg
refl1=0.15
refl2=0.5

[DepthOfField]
dof=1
focalDistance=-17.5
focalRange=3.0
maxBlurRadius=10
```

---

## Pipeline de rendu (mode Tiles)

```
1.  Projection des vertices → espace écran (GetWorldToScreen)
2.  Frustum culling (AABB + triangle)
3.  Backface culling
4.  Calcul smooth normals (une fois au chargement)
5.  Gouraud / Flat shading par sommet
6.  Tri des polygones par Z (qsort)
7.  Tile binning — assignation des triangles aux tuiles
    └── AABB overlap + TriangleIntersectsRegion

--- Worker threads (parallèle) ---

8.  Phase SKYBOX (par tranches de lignes)
    └── SampleEquirectangularUV via LUT précalculée
9.  Reset depthBuffer (distribué sur tous les threads)
10. Phase TRIANGLES (par tiles)
    ├── Edge functions SIMD AVX2 (8 pixels/cycle)
    ├── Coordonnées barycentriques
    ├── Interpolation Z, UV, normales, position 3D
    ├── Normal mapping (TBN)
    ├── Texture sampling
    ├── Environment mapping + Fresnel
    ├── Blinn-Phong lighting
    └── Écriture framebuffer + depthBuffer
11. Phase DOF — 3 passes hexagonales
    ├── Pass 1 : horizontal       (framebuffer → framebufferBlur)
    ├── Pass 2 : diagonale  60°   (framebufferBlur → framebuffer)
    └── Pass 3 : diagonale -60°   (framebuffer → framebufferBlur → framebuffer)

--- Thread main ---

12. UpdateTexture + DrawTexture → affichage Raylib
13. HUD (FPS, triangles, position, DoF params)
```

---

## Compilation

```bash
gcc src/main.c src/globals.c src/utils.c src/skybox.c src/shading.c \
    src/warnock.c src/zbuffer.c src/frustum.c src/tiles.c src/config.c \
    -I include -O2 -mavx2 -mfma -static \
    -o SoftRender3D.exe \
    -lraylib -lwinmm -lgdi32 -lopengl32 -luser32 -lkernel32 -lpthread
```

Ou via VS Code avec la tâche **"build raylib SoftRender3D"** (`Ctrl+Shift+B`).

---

## Contrôles

### Caméra
| Touche | Action |
|--------|--------|
| ↑ ↓ | Déplacer caméra Y |
| ← → | Déplacer caméra X |
| W S | Déplacer caméra Z |
| V T | Déplacer target Y |
| F H | Déplacer target X |
| P L | Déplacer target Z |

### Depth of Field
| Touche | Action |
|--------|--------|
| Pavé `+` | Focus plus proche |
| Pavé `-` | Focus plus loin |
| Pavé `*` | Zone nette plus large |
| Pavé `/` | Zone nette plus étroite |
| Pavé `0` | Augmenter blur max |
| Pavé `1` | Diminuer blur max |

---

## Modèles testés

| Modèle | Triangles | Notes |
|--------|-----------|-------|
| Suzanne (Blender) | ~960 | Modèle de référence |
| Teapot | ~2256 | Avec texture UV et normal map |
| Tank | ~6064 | Modèle complexe |
| Donut | ~1500 | Test smooth normals |

---

## Dépendances

- [Raylib](https://www.raylib.com/) — fenêtre, input, texture, projection
- [Raymath](https://github.com/raysan5/raylib/blob/master/src/raymath.h) — vecteurs, matrices
- POSIX Threads (`pthread`) — multithreading
- Intel AVX2 — SIMD (`immintrin.h`)

---

## Captures d'écran

*(à ajouter)*

---

## Auteur

Projet développé de A à Z en C, dans un but d'apprentissage des algorithmes de rendu 3D bas niveau.

---

## Licence

MIT
