#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

#include "bsp/esp-bsp.h"     // M5Stack CoreS3 BSP (espressif/m5stack_core_s3)
#include "lvgl.h"

#include "menu_nav.h"
#include "ui_screens.h"
#include "encoder_unit.h"
#include "sound_hooks.h"

static const char *TAG = "app_main";

/* 「決定」操作: エンコーダーのボタンを押してすぐ離した場合
 * 「キャンセル」操作: CANCEL_HOLD_MS以上長押ししてから離した場合
 * (専用の取り消しボタンがまだ無いための暫定措置。将来タクトスイッチを
 *  追加する場合は、その入力から直接nav_back()を呼ぶ形に差し替えればよい) */
#define CANCEL_HOLD_MS    600
#define POLL_INTERVAL_MS   15

/* 押しボタン付きロータリーエンコーダーは、軸を押し込む/離す動作そのものが
 * わずかな回転として拾われることがある(機構的なガタ)。ボタンの状態が
 * 変化した直後 BUTTON_JITTER_GUARD_MS の間は回転差分の適用を止めることで、
 * 「長押し中/離した直後に意図せず選択が動く」現象を抑える。 */
#define BUTTON_JITTER_GUARD_MS  150

/* 【診断モード】1にすると通常のnav処理を行わず、レジスタの生値をログに
 * 出し続けるだけになる。エンコーダーのレジスタ配置が未確認のため、
 * ダイヤルを回しながらどのバイトが変化するかを目視で特定するために使う。
 * 使い方: 1にしてビルド・書き込み → シリアルモニタを見ながらダイヤルを
 * ゆっくり回す/ボタンを押す → 変化したレジスタのアドレスと挙動を教えてほしい。 */
#define ENCODER_REGISTER_SCAN_MODE  0

void app_main(void)
{
    ESP_LOGI(TAG, "food printer prototype0 starting");

    /* CoreS3のディスプレイ・タッチ・LVGL処理タスクを初期化。
     * すでに他の場所で bsp_display_start() を呼んでいる場合は二重に呼ばないこと。
     * bsp_display_lock()/unlock() の関数名・シグネチャは使用中のBSPバージョンで
     * 確認してほしい(esp_lvgl_portベースのBSPで一般的な形を想定している)。 */
    bsp_display_start();
    bsp_display_backlight_on();

    ESP_ERROR_CHECK(encoder_init());

#if ENCODER_REGISTER_SCAN_MODE
    ESP_LOGW(TAG, "ENCODER_REGISTER_SCAN_MODE=1: レジスタダンプのみ実行し、UIは初期化しません");
    while (1) {
        /* 候補になりそうな領域をまとめて表示する。0x00からの4byteが有力候補、
         * 0x10/0x20付近も念のため見ておく。 */
        encoder_debug_dump_registers(0x00, 8);
        encoder_debug_dump_registers(0x10, 8);
        encoder_debug_dump_registers(0x20, 4);
        vTaskDelay(pdMS_TO_TICKS(300));
    }
#endif

    sound_hooks_init();

    nav_init();

    bsp_display_lock(0);
    ui_screens_init();
    bsp_display_unlock();

    bool button_was_pressed = false;
    TickType_t press_started_tick = 0;
    TickType_t last_button_edge_tick = 0;
    bool have_button_edge = false;

    while (1) {
        int32_t delta = 0;
        bool button_pressed = false;
        esp_err_t err = encoder_poll(&delta, &button_pressed);

        if (err == ESP_OK) {
            TickType_t now = xTaskGetTickCount();
            bool within_jitter_guard =
                have_button_edge &&
                ((now - last_button_edge_tick) * portTICK_PERIOD_MS < BUTTON_JITTER_GUARD_MS);

            if (delta != 0 && !within_jitter_guard) {
                nav_move_selection(delta);
                sound_hooks_play(UI_SOUND_MOVE);
                bsp_display_lock(0);
                ui_screens_sync_selection();
                bsp_display_unlock();
            } else if (delta != 0 && within_jitter_guard) {
                ESP_LOGD(TAG, "ignoring delta=%ld near a button edge (jitter guard)", (long)delta);
            }

            if (button_pressed && !button_was_pressed) {
                /* 押し始め: 画面遷移とは独立して、まず音だけ鳴らす */
                sound_hooks_play(UI_SOUND_BUTTON);
                press_started_tick = xTaskGetTickCount();
                last_button_edge_tick = press_started_tick;
                have_button_edge = true;
            } else if (!button_pressed && button_was_pressed) {
                last_button_edge_tick = xTaskGetTickCount();
                have_button_edge = true;
                /* 離した瞬間: 押していた時間の長さで決定/キャンセルを判定する */
                TickType_t held_ms =
                    (xTaskGetTickCount() - press_started_tick) * portTICK_PERIOD_MS;

                bool need_refresh = true;
                if (held_ms >= CANCEL_HOLD_MS) {
                    nav_back();
                    sound_hooks_play(UI_SOUND_BACK);
                } else {
                    bool finished = nav_confirm();
                    sound_hooks_play(finished ? UI_SOUND_DONE : UI_SOUND_CONFIRM);
                    if (finished) {
                        ESP_LOGI(TAG, "all parameters confirmed - resetting to major category (demo loop)");
                        nav_init();
                    }
                }

                if (need_refresh) {
                    bsp_display_lock(0);
                    ui_screens_refresh();
                    bsp_display_unlock();
                }
            }
            button_was_pressed = button_pressed;
        } else {
            ESP_LOGW(TAG, "encoder_poll failed: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}
