#include "frustum.h"
#include <math.h>

Frustum extractFrustum(Matrix vp)
{
    Frustum f;

    f.planes[0].normal = (Vector3){ vp.m3+vp.m0,  vp.m7+vp.m4,  vp.m11+vp.m8  }; f.planes[0].d = vp.m15+vp.m12;
    f.planes[1].normal = (Vector3){ vp.m3-vp.m0,  vp.m7-vp.m4,  vp.m11-vp.m8  }; f.planes[1].d = vp.m15-vp.m12;
    f.planes[2].normal = (Vector3){ vp.m3+vp.m1,  vp.m7+vp.m5,  vp.m11+vp.m9  }; f.planes[2].d = vp.m15+vp.m13;
    f.planes[3].normal = (Vector3){ vp.m3-vp.m1,  vp.m7-vp.m5,  vp.m11-vp.m9  }; f.planes[3].d = vp.m15-vp.m13;
    f.planes[4].normal = (Vector3){ vp.m3+vp.m2,  vp.m7+vp.m6,  vp.m11+vp.m10 }; f.planes[4].d = vp.m15+vp.m14;
    f.planes[5].normal = (Vector3){ vp.m3-vp.m2,  vp.m7-vp.m6,  vp.m11-vp.m10 }; f.planes[5].d = vp.m15-vp.m14;

    for (int i = 0; i < 6; i++) {
        float len = Vector3Length(f.planes[i].normal);
        f.planes[i].normal = Vector3Scale(f.planes[i].normal, 1.0f / len);
        f.planes[i].d     /= len;
    }

    return f;
}

bool aabbInFrustum(Frustum* f, Vector3 aabbMin, Vector3 aabbMax)
{
    for (int i = 0; i < 6; i++) {
        Vector3 n = f->planes[i].normal;
        Vector3 pPos = {
            n.x >= 0 ? aabbMax.x : aabbMin.x,
            n.y >= 0 ? aabbMax.y : aabbMin.y,
            n.z >= 0 ? aabbMax.z : aabbMin.z
        };
        if (Vector3DotProduct(n, pPos) + f->planes[i].d < 0)
            return false;
    }
    return true;
}

bool triangleInFrustum(Frustum* f, Vector3 v0, Vector3 v1, Vector3 v2)
{
    for (int i = 0; i < 6; i++) {
        Vector3 n = f->planes[i].normal;
        float   d = f->planes[i].d;
        if (Vector3DotProduct(n,v0)+d < 0 &&
            Vector3DotProduct(n,v1)+d < 0 &&
            Vector3DotProduct(n,v2)+d < 0)
            return false;
    }
    return true;
}

Matrix buildProjectionMatrix(RenderContext* ctx)
{
    float aspect = (float)ctx->screenWidth / (float)ctx->screenHeight;
    float fovy   = ctx->fov * DEG2RAD;
    return MatrixPerspective(fovy, aspect, ctx->near, ctx->far);
}

bool isBackFace(Vector3 v0, Vector3 v1, Vector3 v2, Vector3 cameraPos)
{
    Vector3 e1     = Vector3Subtract(v1, v0);
    Vector3 e2     = Vector3Subtract(v2, v0);
    Vector3 normal = Vector3CrossProduct(e1, e2);
    Vector3 toCamera = Vector3Subtract(cameraPos, v0);
    return Vector3DotProduct(normal, toCamera) >= 0;
}
