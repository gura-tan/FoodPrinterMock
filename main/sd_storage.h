#pragma once
#include <stdbool.h>
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
 * 【要確認】bsp_sdcard_mount() / bsp_sdcard_unmount() の関数名・シグネチャは
 * espressif/m5stack_core_s3 のバージョン(現在3.0.2)によって異なる可能性がある。
 * ビルドエラーになった場合は
 * managed_components/espressif__m5stack_core_s3/include/bsp/esp-bsp.h
 * を確認し、sd_storage.c内の該当行のみ実際の関数名に差し替えればよい。
 */

/* SDカードをマウントする。すでにマウント済みならESP_OKを即返す。
 * カード未挿入・フォーマット不正等で失敗してもアプリは継続動作させたいため、
 * 呼び出し側でESP_ERROR_CHECK()は使わず、戻り値を見てログ+続行すること。 */
esp_err_t sd_storage_mount(void);

void sd_storage_unmount(void);

bool sd_storage_is_mounted(void);

/* マウントポイントの文字列 (例: "/sdcard")。CONFIG_BSP_SD_MOUNT_POINT をそのまま返す。 */
const char *sd_storage_mount_point(void);

#ifdef __cplusplus
}
#endif
