#include "sd_storage.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sd_storage";
static bool s_mounted;

/* 【2026/08/08 実機調査】カード自体はPCのカードリーダーではFAT32で正常に
 * 読み書きできる状態なのに、CoreS3上ではbsp_sdcard_sdspi_mount()内の
 * sdmmc_card_init()がESP_ERR_TIMEOUT(0x107)で失敗する事象を確認。
 * bsp_enable_feature(BSP_FEATURE_SD)でカード電源(AXP2101 ALDO4)をONに
 * した直後、電源安定待ちを一切入れずにSPIプローブ(CMD0等)を投げているため、
 * 電源が安定しきる前に初回コマンドが失敗している可能性がある。
 * BSP側(managed_components配下、vendor管理)は変更せず、こちら側で
 * 電源安定待ちの短いディレイと数回のリトライを行うことで対処する。 */
#define SD_MOUNT_SETTLE_DELAY_MS  50
#define SD_MOUNT_RETRY_COUNT      3
#define SD_MOUNT_RETRY_DELAY_MS   150

esp_err_t sd_storage_mount(void)
{
    if (s_mounted) {
        return ESP_OK;
    }

    /* 【2026/08/07 Notion調査で判明・確認済み】CoreS3はSDカードスロットと
     * LCD(ili9341)が同じSPIバスを共有しており、SDMMC(専用線)経由の接続が
     * 存在しない。espressif/m5stack_core_s3の汎用bsp_sdcard_mount()は
     * SDMMC経路を試みて常にESP_ERR_NOT_SUPPORTED(またはこのプロジェクトの
     * ログで見えたようなESP_ERR_TIMEOUT)を返すため、CoreS3では使えない。
     * 代わりにbsp_sdcard_sdspi_mount()をSPIモードで明示的に呼ぶ必要がある。
     * cfgを{0}で初期化すれば、bsp_sdcard_get_sdspi_host()/get_sdspi_slot()
     * 経由でCoreS3用のデフォルト設定(SPI3ホスト、CSピン等)が自動補完される。 */
    vTaskDelay(pdMS_TO_TICKS(SD_MOUNT_SETTLE_DELAY_MS));

    esp_err_t err = ESP_FAIL;
    for (int attempt = 1; attempt <= SD_MOUNT_RETRY_COUNT; attempt++) {
        /* cfg->host/cfg->slot.sdspiはbsp_sdcard_sdspi_mount()内部のスタック
         * 変数を指すため、リトライのたびに必ず作り直す(使い回すとダングリング
         * ポインタになる)。 */
        bsp_sdcard_cfg_t cfg = {0};
        err = bsp_sdcard_sdspi_mount(&cfg);
        if (err == ESP_OK) {
            break;
        }
        ESP_LOGW(TAG, "bsp_sdcard_sdspi_mount attempt %d/%d failed: %s",
                 attempt, SD_MOUNT_RETRY_COUNT, esp_err_to_name(err));
        if (attempt < SD_MOUNT_RETRY_COUNT) {
            vTaskDelay(pdMS_TO_TICKS(SD_MOUNT_RETRY_DELAY_MS));
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG,
                 "bsp_sdcard_sdspi_mount failed after %d attempts: %s "
                 "(SDカードが挿さっているか、FAT32/exFATでフォーマットされているか確認してください)",
                 SD_MOUNT_RETRY_COUNT, esp_err_to_name(err));
        return err;
    }

    s_mounted = true;
    ESP_LOGI(TAG, "sd card mounted at %s", sd_storage_mount_point());
    return ESP_OK;
}

void sd_storage_unmount(void)
{
    if (!s_mounted) {
        return;
    }
    bsp_sdcard_unmount(); // アンマウントは汎用APIのままでよい(mountと違い機種分岐不要)
    s_mounted = false;
    ESP_LOGI(TAG, "sd card unmounted");
}

bool sd_storage_is_mounted(void)
{
    return s_mounted;
}

const char *sd_storage_mount_point(void)
{
    return CONFIG_BSP_SD_MOUNT_POINT;
}
