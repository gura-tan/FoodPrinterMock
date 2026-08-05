idf_component_register(
    SRCS
        "app_main.c"
        "menu_data.c"
        "menu_nav.c"
        "ui_screens.c"
        "encoder_unit.c"
        "sound_hooks.c"
    INCLUDE_DIRS "."
    EMBED_FILES
        "sounds/button.wav"   # 実行前にこのパスへ実ファイルを置くこと(README参照)
        "sounds/move.wav"     # 実行前にこのパスへ実ファイルを置くこと(README参照)
    REQUIRES
        # managed_components/ 配下の実フォルダ名(espressif__m5stack_core_s3 等)から
        # ベンダー名プレフィックスを外したものがコンポーネント名になる
        m5stack_core_s3   # bsp_display_start() / bsp_audio_codec_speaker_init() 等
        esp_lvgl_port      # bsp_display_lock() / bsp_display_unlock()
        lvgl               # lv_* 一式
        esp_codec_dev      # esp_codec_dev_open/write/close/set_out_vol
)
