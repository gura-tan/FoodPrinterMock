#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * M5Stack Unit Scroll (U186) のESP-IDFドライバ。encoder_unit.h と同じ形のAPIで、
 * Port.B(SDA=GPIO9 黄 / SCL=GPIO8 白)にソフトウェアI2C(soft_i2c.c)で接続する。
 * Port.AのEncoder(I2C_NUM_0)とは別バスなので、同じI2Cアドレス0x40でも衝突しない。
 *
 * ================================================================
 * 【未検証事項】以下の定数はすべて実機で確認するまで確定しない。
 * 公式ドキュメント(https://docs.m5stack.com/ja/unit/UNIT-Scroll)では通信
 * プロトコルが画像のみで、文字として確認できていない。値はEncoder(U135)で
 * 実機確認済みの配置を仮置きしたもの。
 * 確認手順: app_main.cの SCROLL_REGISTER_SCAN_MODE を1にして書き込み、
 * Scrollを回す/押しながらシリアルログを見て、どのレジスタのどのバイトが
 * どう変化するかを確認する(app_main.c内の該当コメント参照)。
 *
 * 各定数は#ifndefで囲んであるので、コードを触らずCMakeLists.txtの
 * target_compile_definitions等で上書きすることもできる。
 * ================================================================
 */

/* I2Cアドレス。EncoderもScrollも0x40だが、バスが別(Port.A/Port.B)なので衝突しない。 */
#ifndef SCROLL_I2C_ADDR
#define SCROLL_I2C_ADDR              0x40
#endif

/* TODO(未検証): カウンタのレジスタ。Encoderと同じ0x10(int16, リトルエンディアン,
 * 符号付き)と推測している。 */
#ifndef SCROLL_REG_COUNTER
#define SCROLL_REG_COUNTER           0x10
#endif

/* TODO(未検証): ボタンのレジスタ(1byte)。Encoderと同じ0x20と推測している。 */
#ifndef SCROLL_REG_BUTTON
#define SCROLL_REG_BUTTON            0x20
#endif

/* TODO(未検証): 1ノッチ(=UI上の1ステップ)あたりの生カウンタ増分。
 * Encoderは1クリックで±2。Scrollはホイール12パルス/回転で、増分が異なる可能性がある。
 * 値が合っていないと「1ノッチで選択が動かない」「2項目動く」等になる。 */
#ifndef SCROLL_RAW_TICKS_PER_STEP
#define SCROLL_RAW_TICKS_PER_STEP    2
#endif

/* TODO(未検証): 回転方向の符号。+1なら生カウンタが増える向きを「選択を先へ進める」
 * (delta>0)とする。実機で逆だったら-1にする。 */
#ifndef SCROLL_DIRECTION_SIGN
#define SCROLL_DIRECTION_SIGN        1
#endif

/* TODO(未検証): ボタンレジスタが「押されている」ときの値。Encoderは0=押下/1=離。
 * 極性が逆だと、起動直後から「押されっぱなし」と判定されてデバッグ選択画面に
 * 入り続ける(最初に疑うこと)。 */
#ifndef SCROLL_BUTTON_PRESSED_LEVEL
#define SCROLL_BUTTON_PRESSED_LEVEL  0
#endif

_Static_assert(SCROLL_RAW_TICKS_PER_STEP > 0, "SCROLL_RAW_TICKS_PER_STEP must be positive");
_Static_assert(SCROLL_DIRECTION_SIGN == 1 || SCROLL_DIRECTION_SIGN == -1,
               "SCROLL_DIRECTION_SIGN must be +1 or -1");

/* ソフトI2Cを初期化し、起動直後のカウンタ値を基準値として読む。
 * カウンタの読み出しに失敗したら(未接続、レジスタ配置違いなど)ESP_ERRを返し、
 * 以降のscroll_poll()はESP_ERR_INVALID_STATEを返す。
 * ただしバス自体の初期化が済んでいれば、scroll_debug_dump_registers()は使える
 * (レジスタ配置が違って初期化に失敗した場合こそ診断が必要なため)。 */
esp_err_t scroll_init(void);

/* 前回読み取りからの回転ステップ差分(1ノッチ=1ステップに正規化、
 * SCROLL_DIRECTION_SIGN適用済み)とボタン状態を取得する。encoder_poll()と同じ
 * 使い方で、どちらの引数もNULL可。戻り値がESP_OKのときのみ出力が有効。
 * 通信に失敗した場合、次に成功したときの最初の1回はカウンタを読み直して
 * 基準値にするだけで(差分は0)、途中の抜けを1つの大きな回転として返さない。 */
esp_err_t scroll_poll(int32_t *out_delta, bool *out_button_pressed);

/* 【診断用】start_regからlengthバイトを生のまま読んでログに出す
 * (app_main.cのSCROLL_REGISTER_SCAN_MODE参照)。 */
void scroll_debug_dump_registers(uint8_t start_reg, uint8_t length);

#ifdef __cplusplus
}
#endif
