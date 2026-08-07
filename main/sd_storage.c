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

    /* 【要確認】BSPバージョンによっては引数の有無やデフォルト設定が異なる
     * 場合がある。espressif/m5stack_core_s3 3.0.2 のヘッダで実際のシグネチャを
     * 確認してほしい(sd_storage.h側にも同じ注記あり)。 */
    esp_err_t err = bsp_sdcard_mount();
    if (err != ESP_OK) {
        ESP_LOGE(TAG,
                 "bsp_sdcard_mount failed: %s "
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
    bsp_sdcard_unmount();
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
