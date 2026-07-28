#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "FoodPrinterMock";

void app_main(void)
{
    /* ディスプレイ初期化（LVGL込み） */
    bsp_display_start();
    bsp_display_backlight_on();

    ESP_LOGI(TAG, "Display started, drawing UI");

    /* LVGL操作はロックを取ってから行う */
    bsp_display_lock(0);

    lv_obj_t *scr = lv_scr_act();

    /* 背景を白に */
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);

    /* 中央にテキストラベルを配置 */
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "FoodPrinter Mock UI");
//    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

    bsp_display_unlock();
}
