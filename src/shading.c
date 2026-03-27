#include "shading.h"
#include <math.h>

void flatShading(RenderContext* ctx, Poly* p, Matrix view)
{
    Vector3 v0 = Vector3Transform(p->v0, view);
    Vector3 v1 = Vector3Transform(p->v1, view);
    Vector3 v2 = Vector3Transform(p->v2, view);

    Vector3 e1 = Vector3Subtract(v1, v0);
    Vector3 e2 = Vector3Subtract(v2, v0);

    Vector3 normal = Vector3CrossProduct(e1, e2);
    normal = Vector3Normalize(normal);

    float intensity = Vector3DotProduct(normal, ctx->lightDir);
    float ambient   = ctx->ambient;
    intensity = fmaxf(intensity, ambient);

    p->couleur = (Color){
        (unsigned char)(p->couleur.r * intensity),
        (unsigned char)(p->couleur.g * intensity),
        (unsigned char)(p->couleur.b * intensity),
        255
    };
}

void gouraudShading(RenderContext* ctx, Poly* p)
{
    float i0 = fmaxf(Vector3DotProduct(p->n0, ctx->lightDir), ctx->ambient);
    float i1 = fmaxf(Vector3DotProduct(p->n1, ctx->lightDir), ctx->ambient);
    float i2 = fmaxf(Vector3DotProduct(p->n2, ctx->lightDir), ctx->ambient);

    p->c0 = (Color){
        (unsigned char)(p->couleur.r * i0),
        (unsigned char)(p->couleur.g * i0),
        (unsigned char)(p->couleur.b * i0),
        255
    };
    p->c1 = (Color){
        (unsigned char)(p->couleur.r * i1),
        (unsigned char)(p->couleur.g * i1),
        (unsigned char)(p->couleur.b * i1),
        255
    };
    p->c2 = (Color){
        (unsigned char)(p->couleur.r * i2),
        (unsigned char)(p->couleur.g * i2),
        (unsigned char)(p->couleur.b * i2),
        255
    };
}
