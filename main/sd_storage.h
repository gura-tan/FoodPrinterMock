#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SDカード(FAT)のマウント管理。
 *
 * BSPの CONFIG_BSP_SD_MOUNT_POINT ("/sdcard") 設定をそのまま利用する。
 * 音声読み込み専用にせず、将来的に操作ログ(バックログにある
 * 「operation logging」タスク)など他用途でも同じマウント状態を
 * 共有して使えるよう、独立したモジュールとして切り出している。
 *
 * 【2026/08/07 判明・確認済み】CoreS3はSDカードスロットとLCD(ili9341)が
 * 同じSPIバスを共有しており、SDMMC(専用線)経由の接続が存在しない。
 * そのため実装(sd_storage.c)では汎用の bsp_sdcard_mount() ではなく
 * bsp_sdcard_sdspi_mount() をSPIモードで明示的に呼んでいる。
 * 詳細はNotion「表現 フードプリンター」ページの「SDカード」セクション参照。
 */

/* SDカードをマウントする。すでにマウント済みならESP_OKを即返す。
 * カード未挿入・フォーマット不正等で失敗してもアプリは継続動作させたいため、
 * 呼び出し側でESP_ERROR_CHECK()は使わず、戻り値を見てログ+続行すること。
 * (内部ではCoreS3向けにbsp_sdcard_sdspi_mount()を呼んでいる) */
esp_err_t sd_storage_mount(void);

void sd_storage_unmount(void);

bool sd_storage_is_mounted(void);

/* マウントポイントの文字列 (例: "/sdcard")。CONFIG_BSP_SD_MOUNT_POINT をそのまま返す。 */
const char *sd_storage_mount_point(void);

#define SD_STORAGE_PRESET_NAME_MAX  32
#define SD_STORAGE_MAX_PRESET_DIRS  16

/* <mount>/sounds 直下のディレクトリ名一覧を取得する(デバッグ用プリセット
 * 選択画面向け)。names[0..戻り値-1]に格納し、戻り値は実際に見つかった数
 * (max_countで頭打ち)。マウントされていない/soundsディレクトリが無い等の
 * 失敗時は0を返す。
 *
 * 【重要】sound_hooks_init()のwavロードと同じ制約: bsp_display_start()を
 * 呼んだ後はSDカードへの実際の読み込みが100%失敗するため、この関数は
 * 必ずbsp_display_start()より前に呼ぶこと。 */
size_t sd_storage_list_preset_dirs(char names[][SD_STORAGE_PRESET_NAME_MAX], size_t max_count);

#ifdef __cplusplus
}
#endif
