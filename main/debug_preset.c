#include "debug_preset.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "debug_preset";

#define NVS_NAMESPACE "dbg_preset"
#define NVS_KEY       "name"

esp_err_t debug_preset_get(char *out, size_t out_size)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        // 名前空間が一度も作られていない(=一度も設定されていない)場合も
        // ESP_ERR_NVS_NOT_FOUNDが返る
        return err;
    }

    size_t len = out_size;
    err = nvs_get_str(handle, NVS_KEY, out, &len);
    nvs_close(handle);
    return err;
}

esp_err_t debug_preset_set(const char *name)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open(RW) failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(handle, NVS_KEY, name);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "debug preset \"%s\" saved to NVS", name);
    } else {
        ESP_LOGE(TAG, "failed to save debug preset \"%s\": %s", name, esp_err_to_name(err));
    }
    return err;
}

esp_err_t debug_preset_clear(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open(RW) failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_erase_key(handle, NVS_KEY);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK; // 元々未設定なら消去成功扱いでよい
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "debug preset override cleared (preset.txt/defaultに戻ります)");
    } else {
        ESP_LOGE(TAG, "failed to clear debug preset: %s", esp_err_to_name(err));
    }
    return err;
}
