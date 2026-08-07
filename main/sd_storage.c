#include "sd_storage.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"

static const char *TAG = "sd_storage";
static bool s_mounted;

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
    bsp_sdcard_cfg_t cfg = {0};
    esp_err_t err = bsp_sdcard_sdspi_mount(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,
                 "bsp_sdcard_sdspi_mount failed: %s "
                 "(SDカードが挿さっているか、FAT32/exFATでフォーマットされているか確認してください)",
                 esp_err_to_name(err));
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
