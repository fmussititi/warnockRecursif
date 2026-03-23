# SoftRenderer3D

Un moteur de rendu 3D software implémenté entièrement sur CPU en C, avec Raylib pour la fenêtre et l'affichage.

> Projet pédagogique visant à comprendre les algorithmes de rendu 3D from scratch, sans GPU.

---

## Fonctionnalités

### Algorithmes de rendu
- **Warnock** — algorithme récursif de subdivision de régions avec Z-buffer hybride
- **Z-Buffer** — rendu classique pixel par pixel avec test de profondeur
- **Tiles** — rendu par tuiles 32×32 pixels avec multithreading

### Éclairage & Shading
- **Flat shading** — une couleur par triangle
- **Phong shading** — interpolation des normales par pixel
- **Blinn-Phong specular** — reflet blanc brillant configurable (shininess)
- **Smooth normals** — calcul automatique par fusion des normales voisines en 2 passes

### Textures
- **Texture mapping** — interpolation des coordonnées UV par pixel
- **Normal mapping** — perturbation des normales via texture (espace tangent, matrice TBN)
- Support des formats JPG et PNG

### Performance
- **Multithreading** — 16 threads POSIX avec synchronisation par barrières (`pthread_barrier`)
- **Tile binning** — assignation des triangles aux tuiles via AABB + test d'intersection
- **Z-buffer local** — un Z-buffer par tuile pour éviter les conflits entre threads
- **Backface culling** optionnel

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

- [Raylib](https://www.raylib.com/) — fenêtre, input, projection 3D
- [Raymath](https://github.com/raysan5/raylib/blob/master/src/raymath.h) — mathématiques 3D (vecteurs, matrices)
- POSIX Threads (`pthread`) — multithreading

---

## Compilation

```bash
gcc main.c -O2 -mavx2 -mfma -o SoftRenderer3D -lraylib -lm -lpthread
```

Ou avec un Makefile :

```bash
make
```

---

## Utilisation

Modifier les defines en haut de `main.c` pour changer de mode de rendu :

```c
#define WARNOCK  0   // Activer le rendu Warnock
#define ZBUFFER  0   // Activer le rendu Z-Buffer
#define TILES    1   // Activer le rendu Tiles (recommandé)
```

Changer le modèle chargé :
```c
Model model = LoadModel("teapotUV.obj");
```

Changer les textures :
```c
ctx.texImage  = LoadImage("diffuse.jpg");
ctx.normalMap = LoadImage("normal.jpg");
```

Contrôles caméra (mode Tiles) :
| Touche | Action |
|--------|--------|
| ↑ ↓ | Déplacer caméra Y |
| ← → | Déplacer caméra X |
| W S | Déplacer caméra Z |

---

## Architecture

```
main.c          — boucle principale, projection, pipeline CPU
warnock.h       — structs Poly, Region, RenderContext, ThreadData
```

### Pipeline de rendu (mode Tiles)

```
1. Projection des vertices → espace écran
2. Calcul des smooth normals (une fois au chargement)
3. Construction des visiblePolys (backface culling, éclairage)
4. Tri des polygones par Z (qsort)
5. Tile binning — assignation des triangles aux tuiles
6. Rasterisation par tiles (multithreadée)
   ├── Clear du framebuffer local
   ├── Edge functions (rasterizer incrémental)
   ├── Interpolation Z, UV, normales, position 3D
   ├── Phong + Blinn-Phong + Normal mapping
   └── Écriture dans le framebuffer global
7. UpdateTexture + DrawTexture → affichage Raylib
```

---

## Captures d'écran

*(à ajouter)*

---

## Auteur

Projet développé de A à Z en C, dans un but d'apprentissage des algorithmes de rendu 3D bas niveau.

---

## Licence

MIT
