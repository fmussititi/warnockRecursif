#include "skybox.h"
#include <math.h>

Vector3 GetRayDirection(RenderContext* ctx, int px, int py)
{
    float ndcX = (2.0f * px / ctx->screenWidth  - 1.0f);
    float ndcY = (1.0f - 2.0f * py / ctx->screenHeight);

    float fov    = ctx->fov * DEG2RAD;
    float aspect = (float)ctx->screenWidth / ctx->screenHeight;

    float sx = ndcX * tanf(fov * 0.5f) * aspect;
    float sy = ndcY * tanf(fov * 0.5f);

    Vector3 dir = Vector3Add(
        ctx->cameraForward,
        Vector3Add(
            Vector3Scale(ctx->cameraRight, sx),
            Vector3Scale(ctx->cameraUp,    sy)
        )
    );

    return Vector3Normalize(dir);
}

void PrecomputeSkyboxLUT(RenderContext* ctx)
{
    int W = ctx->screenWidth;
    int H = ctx->screenHeight;

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            Vector3 dir = GetRayDirection(ctx, x, y);
            dir = Vector3Normalize(dir);

            float u = 0.5f + atan2f(dir.z, dir.x) / (2.0f * PI);
            float v = 0.5f - asinf(dir.y) / PI;

            int idx = y * W + x;
            ctx->skyU[idx] = u;
            ctx->skyV[idx] = v;
        }
    }
}

Color SampleEquirectangular(RenderContext* ctx, Vector3 dir)
{
    if (!ctx->envMap.data) return (Color){255, 0, 255, 255};

    dir = Vector3Normalize(dir);

    float u = 0.5f + atan2f(dir.z, dir.x) / (2.0f * PI);
    float v = 0.5f - asinf(dir.y) / PI;

    int x = (int)(u * ctx->envMap.width);
    int y = (int)(v * ctx->envMap.height);

    x = (x % ctx->envMap.width + ctx->envMap.width) % ctx->envMap.width;
    if (y < 0) y = 0;
    if (y >= ctx->envMap.height) y = ctx->envMap.height - 1;

    Color* pixels = (Color*)ctx->envMap.data;
    return pixels[y * ctx->envMap.width + x];
}

Color SampleEquirectangularUV(RenderContext* ctx, float u, float v)
{
    int x = (int)(u * ctx->envMap.width);
    int y = (int)(v * ctx->envMap.height);

    x = (x % ctx->envMap.width + ctx->envMap.width) % ctx->envMap.width;
    y = Clamp(y, 0, ctx->envMap.height - 1);

    Color* pixels = (Color*)ctx->envMap.data;
    return pixels[y * ctx->envMap.width + x];
}
