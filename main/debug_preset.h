#pragma once
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * デバッグ用プリセット選択画面(app_main.cの起動時ボタン長押し分岐)が選んだ
 * プリセット名を、SDカードではなくNVS(ESP32内蔵フラッシュ)に保存・読み出す
 * ためのモジュール。
 *
 * SDカードを使わない理由: bsp_display_start()を呼んだ後はSDカードへの実際の
 * 読み込み/書き込みが100%失敗する(main/app_main.c冒頭コメント参照)。
 * デバッグ選択画面はディスプレイ表示が前提のため、選択結果の保存にSDカードは
 * 使えない。NVSはGPIO35のピン共有と無関係なため、この制約を受けない。
 *
 * sound_hooks.c(読む側)とapp_main.c(書く側)の両方から使われる、どちらにも
 * 属さない独立した小さい依存として切り出している(sd_storage.cがsound_hooks.c
 * から切り出された経緯と同じ理由)。
 */

/* sound_hooks.cのPRESET_NAME_MAX_LENと合わせてあること */
#define DEBUG_PRESET_NAME_MAX_LEN 48

/* 保存されているプリセット名をoutにコピーする。
 * 一度も選択されていない場合はESP_ERR_NVS_NOT_FOUNDを返す。 */
esp_err_t debug_preset_get(char *out, size_t out_size);

/* プリセット名を保存する(readdir()が返した文字列をそのまま渡すこと。
 * 大文字/小文字を加工しない)。 */
esp_err_t debug_preset_set(const char *name);

/* 保存済みの設定を消去する(「(SDのpreset.txtに戻す)」選択用)。
 * 未設定の状態への消去もESP_OK扱いにする。 */
esp_err_t debug_preset_clear(void);

#ifdef __cplusplus
}
#endif
