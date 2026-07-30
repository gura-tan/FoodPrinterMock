idf_component_register(
    SRCS
        "app_main.c"
        "menu_data.c"
        "menu_nav.c"
        "ui_screens.c"
        "encoder_unit.c"
        "sound_hooks.c"
    INCLUDE_DIRS "."
    REQUIRES
        # managed_components/ 配下の実フォルダ名(espressif__m5stack_core_s3 等)から
        # ベンダー名プレフィックスを外したものがコンポーネント名になる
        m5stack_core_s3   # bsp_display_start() 等 (bsp/esp-bsp.h)
        esp_lvgl_port      # bsp_display_lock() / bsp_display_unlock()
        lvgl               # lv_* 一式
)
