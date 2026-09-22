#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "input_source.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 操作入力(Encoder / Scroll)の窓口。app_main.cは個別のドライバ
 * (encoder_unit / scroll_unit)を直接呼ばず、この窓口経由でポーリングする。
 *
 * 目的は2つ:
 *  1. 入力の種類ごとの違いをここで吸収し、app_mainを入力の数に依存させない
 *     (input_source_tの添字で回せる)。
 *  2. 未接続・通信エラー時のログ出力を1か所に集約する。以前のapp_mainは
 *     ポーリング失敗のたびに15msごとにESP_LOGWを出していたが、未接続の入力が
 *     あっても警告が連打されないよう、ここでは
 *       - 初期化に失敗した入力は静かにスキップ(ESP_ERR_INVALID_STATEを返すだけ)
 *       - 通信エラーは「正常→異常」「異常→正常」に変わったときだけ1回ログ
 *     とする。
 *
 * app_mainのタスクからのみ呼ぶこと(内部状態に排他制御は無い)。
 */

/* 全入力の初期化を試みる。1つでも初期化に成功すればESP_OK、どれも使えなければ
 * ESP_ERR_NOT_FOUND(この場合でもアプリは継続できる)。個々の失敗はここでWARNを出す。 */
esp_err_t input_hub_init(void);

/* 初期化に成功して使える入力かどうか。 */
bool input_hub_is_available(input_source_t src);

/* 1入力を1回ポーリングする。out_delta(1ノッチ=1ステップに正規化済み)・
 * out_button_pressedはどちらもNULL可。
 * 初期化していない入力にはESP_ERR_INVALID_STATEを返す(ログなし)。 */
esp_err_t input_hub_poll(input_source_t src, int32_t *out_delta, bool *out_button_pressed);

/* ログ表示用の名前("encoder" / "scroll")。範囲外なら"?"。 */
const char *input_hub_name(input_source_t src);

#ifdef __cplusplus
}
#endif
