#include "utils.h"
#include <math.h>
#include <stdlib.h>

Color couleurAleatoire(void) {
    Color couleurs[5] = { RED, GREEN, BLUE, YELLOW, ORANGE };
    return couleurs[GetRandomValue(0, 4)];
}

Vector3 getNormal(Mesh mesh, int index, Vector3* smoothNormals) {
    return smoothNormals[index];
}

Vector3 getVertex(Mesh mesh, int index) {
    return (Vector3){
        mesh.vertices[index*3 + 0],
        mesh.vertices[index*3 + 1],
        mesh.vertices[index*3 + 2]
    };
}

Vector2 getUV(Mesh mesh, int index) {
    if (!mesh.texcoords) return (Vector2){0, 0};
    return (Vector2){
        mesh.texcoords[index*2 + 0],
        mesh.texcoords[index*2 + 1]
    };
}

int compare_zmin(const void* a, const void* b) {
    Poly* p1 = (Poly*)a;
    Poly* p2 = (Poly*)b;
    if (p1->zmin < p2->zmin) return -1;
    if (p1->zmin > p2->zmin) return  1;
    return 0;
}

// Voici une version simplifiée de ce que tu dois faire :
// 1. Parcourir tous tes 17406 sommets.
// 2. Pour chaque sommet, vérifier s'il existe déjà dans un nouveau tableau "uniques".
// 3. Si oui, on ajoute la normale du sommet actuel à la normale du sommet unique trouvé.
// 4. Si non, on l'ajoute comme nouveau sommet unique.

// Raylib propose une fonction (souvent dans les extras ou raymath) 
// Mais voici comment le faire proprement pour ton moteur :

Mesh OptimizeMesh(Mesh mesh) {
    Mesh optimized = { 0 };
    
    Vector3 *uniquePositions = malloc(mesh.vertexCount * sizeof(Vector3));
    Vector3 *accumulatedNormals = calloc(mesh.vertexCount, sizeof(Vector3));
    // Ajoutons les UVs pour éviter que le modèle disparaisse
    Vector2 *uniqueTexCoords = malloc(mesh.vertexCount * sizeof(Vector2));
    
    int *indicesMap = malloc(mesh.vertexCount * sizeof(int));
    int uniqueCount = 0;

    for (int i = 0; i < mesh.vertexCount; i++) {
        // Extraction sécurisée des données de l'ancien mesh
        Vector3 v = { mesh.vertices[i*3], mesh.vertices[i*3+1], mesh.vertices[i*3+2] };
        Vector3 n = { mesh.normals[i*3], mesh.normals[i*3+1], mesh.normals[i*3+2] };
        Vector2 uv = { 0 };
        if (mesh.texcoords) {
            uv.x = mesh.texcoords[i*2];
            uv.y = mesh.texcoords[i*2+1];
        }

        int foundIndex = -1;
        for (int j = 0; j < uniqueCount; j++) {
            // On compare la position ET l'UV (optionnel mais conseillé pour les textures)
            if (Vector3Distance(v, uniquePositions[j]) < 0.0001f) {
                foundIndex = j;
                break;
            }
        }

        if (foundIndex >= 0) {
            accumulatedNormals[foundIndex] = Vector3Add(accumulatedNormals[foundIndex], n);
            indicesMap[i] = foundIndex;
        } else {
            uniquePositions[uniqueCount] = v;
            accumulatedNormals[uniqueCount] = n;
            if (mesh.texcoords) uniqueTexCoords[uniqueCount] = uv;
            indicesMap[i] = uniqueCount;
            uniqueCount++;
        }
    }

    // Normalisation
    for (int i = 0; i < uniqueCount; i++) accumulatedNormals[i] = Vector3Normalize(accumulatedNormals[i]);

    // Allocalion du nouveau Mesh
    optimized.vertexCount = uniqueCount;
    optimized.triangleCount = mesh.vertexCount / 3;
    optimized.vertices = (float *)malloc(uniqueCount * 3 * sizeof(float));
    optimized.normals = (float *)malloc(uniqueCount * 3 * sizeof(float));
    optimized.indices = (unsigned short *)malloc(mesh.vertexCount * sizeof(unsigned short));
    if (mesh.texcoords) optimized.texcoords = (float *)malloc(uniqueCount * 2 * sizeof(float));

    // Remplissage
    for (int i = 0; i < uniqueCount; i++) {
        optimized.vertices[i*3+0] = uniquePositions[i].x;
        optimized.vertices[i*3+1] = uniquePositions[i].y;
        optimized.vertices[i*3+2] = uniquePositions[i].z;
        optimized.normals[i*3+0] = accumulatedNormals[i].x;
        optimized.normals[i*3+1] = accumulatedNormals[i].y;
        optimized.normals[i*3+2] = accumulatedNormals[i].z;
        if (mesh.texcoords) {
            optimized.texcoords[i*2+0] = uniqueTexCoords[i].x;
            optimized.texcoords[i*2+1] = uniqueTexCoords[i].y;
        }
    }

    for (int i = 0; i < mesh.vertexCount; i++) {
        ((unsigned short *)optimized.indices)[i] = (unsigned short)indicesMap[i];
    }

    free(uniquePositions);
    free(accumulatedNormals);
    free(uniqueTexCoords);
    free(indicesMap);

    // CRUCIAL pour Raylib
    UploadMesh(&optimized, false); 
    
    return optimized;
}
