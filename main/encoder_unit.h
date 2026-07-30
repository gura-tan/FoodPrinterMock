#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * M5Stack用エンコーダーユニット(U135)のESP-IDFドライバ。
 *
 * CoreS3のPort.A(外部Groveポート, SDA=GPIO2 / SCL=GPIO1)は、BSPが
 * bsp_i2c_init()で自動初期化する内部I2Cバス(SDA=GPIO12/SCL=GPIO11)とは
 * 別バスなので、ここで独自にI2Cバスを初期化している。
 *
 * 【実機で検証済み(2026/07)】
 * - ボタン: レジスタ0x20, 1byte, 0=押されている / 1=離されている
 * - 回転 : レジスタ0x10, 2byte(リトルエンディアン, 符号付き16bit)の
 *          生カウンタ。1クリックの回転で生の値が±2変化する。ただし
 *          ポーリングタイミングによっては±1ずつに分割されて読めることが
 *          あるため、encoder_unit.c側で繰り越し(キャリー)付きの
 *          2ステップ変換を行っている。
 * - レジスタ0x00は無関係(常に固定値を返す、未使用)
 * - AXP2101のPort.A電源レール: 明示的な有効化コードを入れていないが、
 *   実機でボタン・回転とも読み取れたため、少なくとも今回の環境では
 *   追加対応は不要だった。
 */

esp_err_t encoder_init(void);

/* 前回読み取りからのカウンタ差分(delta)とボタン状態を取得する。
 * 戻り値がESP_OKのときのみ *out_delta / *out_button_pressed が有効。 */
esp_err_t encoder_poll(int32_t *out_delta, bool *out_button_pressed);

/* 【診断用】start_regからlengthバイトを生のまま読んでログに出す。
 * レジスタ配置が未確認のため、実機でダイヤルを回しながらどのバイトが
 * 変化するかを目視確認するために使う(app_main.cのENCODER_REGISTER_SCAN_MODE参照)。 */
void encoder_debug_dump_registers(uint8_t start_reg, uint8_t length);

#ifdef __cplusplus
}
#endif
