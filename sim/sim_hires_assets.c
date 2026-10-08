/*
 * UI_SCALE=4(スクリーンショット用の高解像度ビルド)で使うアイコン。
 * main/icons/icon_salt.c は40x40のA8(アルファのみ)画像で、そのまま4倍に
 * 拡大するとエッジがにじむため、起動時に一度だけ160x160へ作り直す:
 *   1) Catmull-Rom(バイキュービック)で4倍にアップサンプル
 *   2) アルファ値にS字カーブをかけて、エッジの境界(アルファ0.5付近)を締める
 *      (元画像は2値マスクをLANCZOSで縮小したもので、アルファ0.5が本来の輪郭)
 * 元の salt.jpeg から160x160で再生成できるなら、そちらの方が正確。その場合は
 * gen_icon_salt.pyの出力サイズを160にして、このファイルの代わりに
 * icon_salt_hi(lv_image_dsc_t)を直接定義すればよい。
 */
#include "lvgl.h"
#include "icons/icon_salt.h"
#include <math.h>

#define HI_SCALE 4
#define HI_W (40 * HI_SCALE)
#define HI_H (40 * HI_SCALE)

static uint8_t s_hi_map[HI_W * HI_H];
static lv_image_dsc_t s_hi_dsc;
static bool s_built;

static float cr_kernel(float t)
{
    t = fabsf(t);
    if (t < 1.0f) return 1.5f * t * t * t - 2.5f * t * t + 1.0f;
    if (t < 2.0f) return -0.5f * t * t * t + 2.5f * t * t - 4.0f * t + 2.0f;
    return 0.0f;
}

static float src_at(int x, int y)
{
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > 39) x = 39;
    if (y > 39) y = 39;
    return icon_salt.data[y * 40 + x] / 255.0f;
}

static float smoothstep(float lo, float hi, float v)
{
    float t = (v - lo) / (hi - lo);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

const lv_image_dsc_t *ui_hires_icon_salt(void)
{
    if (s_built) {
        return &s_hi_dsc;
    }

    for (int y = 0; y < HI_H; y++) {
        float sy = (y + 0.5f) / HI_SCALE - 0.5f;
        int iy = (int)floorf(sy);
        float fy = sy - iy;
        for (int x = 0; x < HI_W; x++) {
            float sx = (x + 0.5f) / HI_SCALE - 0.5f;
            int ix = (int)floorf(sx);
            float fx = sx - ix;

            float acc = 0.0f;
            for (int j = -1; j <= 2; j++) {
                float wy = cr_kernel(fy - j);
                for (int i = -1; i <= 2; i++) {
                    acc += src_at(ix + i, iy + j) * wy * cr_kernel(fx - i);
                }
            }
            float v = smoothstep(0.2f, 0.8f, acc);
            s_hi_map[y * HI_W + x] = (uint8_t)(v * 255.0f + 0.5f);
        }
    }

    s_hi_dsc = icon_salt; /* header以外のフィールド初期値を引き継ぐ */
    s_hi_dsc.header.w = HI_W;
    s_hi_dsc.header.h = HI_H;
    s_hi_dsc.header.stride = HI_W;
    s_hi_dsc.data_size = sizeof(s_hi_map);
    s_hi_dsc.data = s_hi_map;
    s_built = true;
    return &s_hi_dsc;
}
