#include "bsp/esp-bsp.h"

void app_main(void) {
    bsp_display_start();
    bsp_display_backlight_on();
    // 以降、LVGL API or 自前描画
}
