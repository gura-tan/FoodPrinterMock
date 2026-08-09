idf_component_register(
    SRCS
        "app_main.c"
        "menu_data.c"
        "menu_nav.c"
        "ui_screens.c"
        "encoder_unit.c"
        "sound_hooks.c"
        "sd_storage.c"
        "fonts/font_ja_menu_14.c"
    INCLUDE_DIRS "."
    REQUIRES
        # managed_components/ 配下の実フォルダ名(espressif__m5stack_core_s3 等)から
        # ベンダー名プレフィックスを外したものがコンポーネント名になる
        m5stack_core_s3   # bsp_display_start() / bsp_audio_codec_speaker_init() /
                           # bsp_sdcard_mount() 等
        esp_lvgl_port      # bsp_display_lock() / bsp_display_unlock()
        lvgl               # lv_* 一式
        esp_codec_dev      # esp_codec_dev_open/write/close/set_out_vol
)

# 【変更点】UI音声はSDカード(/sdcard/sounds/...)から読み込む方式に変更したため、
# main/sounds/*.wav のEMBED_FILESは廃止した。main/sounds/ ディレクトリと
# README_PLACE_WAV_HERE.txt はもう使わないので削除してよい
# (SDカードへの配置ルールは main/SD_CARD_SOUND_SETUP.md 参照)。
