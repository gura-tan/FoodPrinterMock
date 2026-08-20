#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"

#include "bsp/esp-bsp.h"     // M5Stack CoreS3 BSP (espressif/m5stack_core_s3)
#include "lvgl.h"

#include "menu_nav.h"
#include "ui_screens.h"
#include "encoder_unit.h"
#include "sound_hooks.h"
#include "sd_storage.h"
#include "debug_preset.h"

static const char *TAG = "app_main";

/* 「決定」操作: エンコーダーのボタンを押してすぐ(CANCEL_HOLD_MS未満で)離した場合
 * 「キャンセル」操作: 押している間にCANCEL_HOLD_MSに達した瞬間に確定する
 * (離す操作を待たない)。確定後にボタンを離しても、その離す動作自体には
 * 何も反応しない(音も鳴らさず、画面遷移も行わない)。
 * (専用の取り消しボタンがまだ無いための暫定措置。将来タクトスイッチを
 *  追加する場合は、その入力から直接nav_back()を呼ぶ形に差し替えればよい) */
#define CANCEL_HOLD_MS    500
#define POLL_INTERVAL_MS   15

/* 押しボタン付きロータリーエンコーダーは、軸を押し込む/離す動作そのものが
 * わずかな回転として拾われることがある(機構的なガタ)。ボタンの状態が
 * 変化した直後 BUTTON_JITTER_GUARD_MS の間は回転差分の適用を止めることで、
 * 「長押し中/離した直後に意図せず選択が動く」現象を抑える。 */
#define BUTTON_JITTER_GUARD_MS  50

/* 押しボタンの接点バウンス対策。ボタンの押下/解放そのもの(I2Cレジスタの
 * 生値)にはデバウンス処理が無いため、離した瞬間に接点が跳ねて一瞬だけ
 * 「また押された」と読めることがある。これをそのまま新しい押下エッジとして
 * 扱うとUI_SOUND_HITが再発火し、再生中のPROCEED/DONEのテールを打ち切って
 * しまう。直前に確定したボタンエッジからBUTTON_DEBOUNCE_MS未満で読めた
 * 逆方向の値は無視し、直前の確定状態を維持する。
 * BUTTON_JITTER_GUARD_MS(回転量の適用を止める猶予)とは別物: こちらは
 * ボタンの押下/解放の判定そのものを安定させるためのもので、実際の押下・
 * 解放動作(数十〜数百ms)より十分短く、接点バウンス(数〜十数ms)より
 * 十分長い値にしている。 */
#define BUTTON_DEBOUNCE_MS  30

/* 【デバッグ用プリセット選択】起動直後(ディスプレイ開始前)にボタンが
 * 押されているかどうかを判定するためのサンプリング設定。コールド起動直後の
 * I2Cバス安定待ちノイズやエンコーダーの機械的ガタによる誤検出を避けるため、
 * BOOT_HOLD_SAMPLE_COUNT回連続で押下状態が確認できた場合だけデバッグモードへ
 * 入る(合計 (COUNT-1)*INTERVAL_MS 程度の間、押され続けている必要がある)。 */
#define BOOT_HOLD_SAMPLE_COUNT        5
#define BOOT_HOLD_SAMPLE_INTERVAL_MS 30

/* デバッグ用プリセット選択画面の一覧先頭に置く固定選択肢。選ぶとNVSの
 * デバッグ上書き設定を消去し、通常のSD preset.txt / defaultの挙動に戻す。 */
static const char *const DEBUG_PICKER_RESET_LABEL = "(SDのpreset.txtに戻す)";

/* 【診断モード】1にすると通常のnav処理を行わず、レジスタの生値をログに
 * 出し続けるだけになる。エンコーダーのレジスタ配置が未確認のため、
 * ダイヤルを回しながらどのバイトが変化するかを目視で特定するために使う。
 * 使い方: 1にしてビルド・書き込み → シリアルモニタを見ながらダイヤルを
 * ゆっくり回す/ボタンを押す → 変化したレジスタのアドレスと挙動を教えてほしい。 */
#define ENCODER_REGISTER_SCAN_MODE  0

/* ボタンが離されるまでブロックして待つ(デバッグ選択画面まわりの入力を
 * 必ず「離されている」状態から開始・終了させるためのヘルパー)。
 * encoder_poll()が失敗し続ける場合(エンコーダーが外れた等)は無限待ちに
 * ならないよう、失敗時点で即座に抜ける。 */
static void wait_for_button_release(void)
{
    for (;;) {
        bool pressed = false;
        if (encoder_poll(NULL, &pressed) != ESP_OK || !pressed) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

/* デバッグ用プリセット選択画面を表示し、選択が確定したらNVSに保存して
 * esp_restart()する(正常系ではこの関数は戻らない)。
 * SDカード上に有効なプリセットフォルダが1つも見つからなかった場合だけ、
 * 何もせずに戻る(呼び出し側は通常起動にフォールスルーする)。
 *
 * 呼び出し時点でSDアクセス(プリセットフォルダ列挙)は完了している前提で、
 * この関数の中でbsp_display_start()を行う(以降は本関数を抜けるまで
 * SDカードへ触れないこと)。 */
static void run_debug_preset_picker(void)
{
    char preset_names[SD_STORAGE_MAX_PRESET_DIRS][SD_STORAGE_PRESET_NAME_MAX];
    size_t preset_count = sd_storage_list_preset_dirs(preset_names, SD_STORAGE_MAX_PRESET_DIRS);
    if (preset_count == 0) {
        ESP_LOGW(TAG, "デバッグ選択画面: プリセットフォルダが見つからなかったため、通常起動にフォールバックします");
        return;
    }

    const char *display_names[SD_STORAGE_MAX_PRESET_DIRS + 1];
    display_names[0] = DEBUG_PICKER_RESET_LABEL;
    for (size_t i = 0; i < preset_count; i++) {
        display_names[i + 1] = preset_names[i];
    }
    size_t display_count = preset_count + 1;

    bsp_display_start();
    bsp_display_backlight_on();

    bsp_display_lock(0);
    ui_screens_show_debug_picker(display_names, display_count);
    bsp_display_unlock();

    /* デバッグ分岐に入った時点ではまだボタンが押されている状態なので、
     * 選択画面自体の押下/離す判定は一度「離された」状態にしてから始める */
    wait_for_button_release();

    int selected = 0;
    bool button_was_pressed = false;
    TickType_t last_button_edge_tick = 0;
    bool have_button_edge = false;

    for (;;) {
        int32_t delta = 0;
        bool button_pressed = false;
        esp_err_t err = encoder_poll(&delta, &button_pressed);
        if (err != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
            continue;
        }

        TickType_t now = xTaskGetTickCount();
        bool within_jitter_guard =
            have_button_edge &&
            ((now - last_button_edge_tick) * portTICK_PERIOD_MS < BUTTON_JITTER_GUARD_MS);

        if (delta != 0 && !within_jitter_guard) {
            int next = selected + (int)delta;
            if (next < 0) {
                next = 0;
            }
            if (next >= (int)display_count) {
                next = (int)display_count - 1;
            }
            if (next != selected) {
                selected = next;
                bsp_display_lock(0);
                ui_screens_debug_picker_set_selected(selected);
                bsp_display_unlock();
            }
        }

        if (button_pressed && !button_was_pressed) {
            last_button_edge_tick = now;
            have_button_edge = true;
        } else if (!button_pressed && button_was_pressed) {
            /* 短押しで離した瞬間に確定する */
            last_button_edge_tick = now;
            have_button_edge = true;
            button_was_pressed = button_pressed;
            break;
        }
        button_was_pressed = button_pressed;

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }

    esp_err_t save_err;
    if (selected == 0) {
        save_err = debug_preset_clear();
    } else {
        save_err = debug_preset_set(preset_names[selected - 1]);
    }
    if (save_err != ESP_OK) {
        ESP_LOGW(TAG, "デバッグプリセット選択の保存に失敗しました: %s", esp_err_to_name(save_err));
    }

    /* 押しっぱなしのまま再起動して次回起動時に即デバッグモードへ
     * 戻ってしまうのを防ぐため、もう一度離されるのを待つ */
    wait_for_button_release();

    ESP_LOGI(TAG, "デバッグプリセット選択完了。再起動します");
    esp_restart();
}

void app_main(void)
{
    ESP_LOGI(TAG, "food printer prototype0 starting");

    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);

    /* エンコーダー未接続でも起動を継続できるようにする(以前はESP_ERROR_CHECKで
     * 即abort→再起動ループしていた)。encoder_poll()は未初期化なら
     * ESP_ERR_INVALID_STATEを返すだけで、呼び出し側(下のwhileループ)は
     * 既にエラーを警告ログだけで無視して継続する作りになっているので、
     * ここを緩めるだけで安全に動く。
     * 【デバッグ用プリセット選択】起動時ボタン長押し検出にも使うため、
     * SDマウント・ディスプレイ開始より前に初期化する。エンコーダーは
     * GPIO1/2の専用I2Cバスを使い、LCD/SDが共有するGPIO35とは無関係なため、
     * この位置に移動しても問題ない。 */
    esp_err_t encoder_err = encoder_init();
    if (encoder_err != ESP_OK) {
        ESP_LOGW(TAG, "encoder_init failed: %s (エンコーダー未接続の可能性。切り分けのため起動は継続します)",
                 esp_err_to_name(encoder_err));
    }

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

    /* 【デバッグ用プリセット選択】起動直後にボタンが押されていたら、通常の
     * メニューではなくSDカード上のプリセット選択画面に入る。誤検出を
     * 避けるため、複数回連続で押下確認できた場合だけデバッグモードへ入る。 */
    bool enter_debug_mode = (encoder_err == ESP_OK);
    for (int i = 0; enter_debug_mode && i < BOOT_HOLD_SAMPLE_COUNT; i++) {
        bool pressed = false;
        if (encoder_poll(NULL, &pressed) != ESP_OK || !pressed) {
            enter_debug_mode = false;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(BOOT_HOLD_SAMPLE_INTERVAL_MS));
    }

    if (enter_debug_mode) {
        /* 【重要】bsp_display_start()より前にSDカードへの全アクセス
         * (プリセットフォルダ列挙)を終わらせる必要がある。CoreS3はLCDの
         * DCピンとSD/LCDのMISOピンが同じGPIO35を共有しており、
         * bsp_display_start()を先に呼んでしまうと、その後のSDカードへの
         * 実際の読み込みは100%失敗する(sound_hooks_init()と同じ制約)。 */
        if (sd_storage_mount() == ESP_OK) {
            run_debug_preset_picker(); // プリセットが見つかれば戻らない(esp_restart())
        } else {
            ESP_LOGW(TAG, "デバッグ選択画面: SDカードがマウントできなかったため、通常起動にフォールバックします");
        }
        /* ここに来るのは「プリセットフォルダが見つからなかった」
         * 「SDカードがマウントできなかった」場合のみ。通常起動へ続行する。 */
    }

    /* 【2026/08/08 実機調査で確定】CoreS3はLCDのDCピンとSD/LCDのMISOピンが
     * 同じGPIO35を共有しており、bsp_display_start()(LVGLの常時再描画タスクが
     * DCピンを駆動し続ける)を先に呼んでしまうと、その後のSDカードへの実際の
     * 読み込みは100%失敗する(sdmmc_card_init/sdmmc_read_sectors_dmaが
     * ESP_ERR_TIMEOUTで確実に落ちる)ことを実機切り分けで確認済み。
     * マウントだけでなく実ファイル読み込みまで含めて、必ずLCD起動より前に
     * 完了させる必要があるため、sound_hooks_init()(SDマウント+全wav読み込み)
     * をbsp_display_start()より前に呼ぶ順序に変更している。以前の版で
     * ここが逆順(表示を先に初期化)だったのがSDカードから音が鳴らなかった
     * 直接の原因。 */
    sound_hooks_init();

    /* CoreS3のディスプレイ・タッチ・LVGL処理タスクを初期化。
     * すでに他の場所で bsp_display_start() を呼んでいる場合は二重に呼ばないこと。
     * bsp_display_lock()/unlock() の関数名・シグネチャは使用中のBSPバージョンで
     * 確認してほしい(esp_lvgl_portベースのBSPで一般的な形を想定している)。 */
    bsp_display_start();
    bsp_display_backlight_on();

    nav_init();

    bsp_display_lock(0);
    ui_screens_init();
    bsp_display_unlock();

    bool button_was_pressed = false;
    TickType_t press_started_tick = 0;
    TickType_t last_button_edge_tick = 0;
    bool have_button_edge = false;
    /* 押している間にCANCEL_HOLD_MSへ達してBACKを確定させたかどうか。
     * 確定後、実際にボタンを離した瞬間の処理を無効化するために使う。 */
    bool back_triggered_this_press = false;

    while (1) {
        if (ui_screens_transition_in_progress()) {
            /* 画面遷移のワイプ演出中は入力を一切受け付けない。演出は
             * 片道90msと短いため、この間だけencoder_poll()自体を
             * 呼ばずにスキップしても押しっぱなし長押し(CANCEL_HOLD_MS)の
             * 判定には影響しない(press_started_tickは実時間基準のまま)。 */
            vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
            continue;
        }

        int32_t delta = 0;
        bool button_pressed = false;
        esp_err_t err = encoder_poll(&delta, &button_pressed);

        if (err == ESP_OK) {
            TickType_t now = xTaskGetTickCount();
            bool within_jitter_guard =
                have_button_edge &&
                ((now - last_button_edge_tick) * portTICK_PERIOD_MS < BUTTON_JITTER_GUARD_MS);

            /* 直前に確定したボタンエッジからBUTTON_DEBOUNCE_MS未満しか
             * 経っていない場合、生の読み値が逆転していても接点バウンスと
             * みなして無視し、直前の確定状態(button_was_pressed)を使う。 */
            bool within_button_debounce =
                have_button_edge &&
                ((now - last_button_edge_tick) * portTICK_PERIOD_MS < BUTTON_DEBOUNCE_MS);
            bool debounced_button_pressed = within_button_debounce ? button_was_pressed : button_pressed;

            if (delta != 0 && !within_jitter_guard) {
                nav_move_selection(delta);
                /* PROCEED/DONE再生中でも即座に打ち切って割り込む。実機で
                 * 試した結果、MOVE/HITがPROCEEDのテールを打ち切ってでも
                 * 常に即座に反応したほうが操作感が安定するため。 */
                sound_hooks_play(UI_SOUND_MOVE);
                bsp_display_lock(0);
                ui_screens_sync_selection();
                bsp_display_unlock();
            } else if (delta != 0 && within_jitter_guard) {
                ESP_LOGD(TAG, "ignoring delta=%ld near a button edge (jitter guard)", (long)delta);
            }

            if (debounced_button_pressed && !button_was_pressed) {
                /* 押し始め: 画面遷移とは独立して、まず音だけ鳴らす */
                sound_hooks_play(UI_SOUND_HIT);
                press_started_tick = now;
                last_button_edge_tick = press_started_tick;
                have_button_edge = true;
                back_triggered_this_press = false;
            } else if (debounced_button_pressed && button_was_pressed && !back_triggered_this_press) {
                /* 押され続けている間: 長押し時間(CANCEL_HOLD_MS)に達した瞬間、
                 * 離す操作を待たずにBACKを確定させる */
                TickType_t held_ms = (now - press_started_tick) * portTICK_PERIOD_MS;
                if (held_ms >= CANCEL_HOLD_MS) {
                    nav_back();
                    sound_hooks_play(UI_SOUND_BACK);
                    back_triggered_this_press = true;
                    bsp_display_lock(0);
                    ui_screens_refresh();
                    bsp_display_unlock();
                }
            } else if (!debounced_button_pressed && button_was_pressed) {
                last_button_edge_tick = now;
                have_button_edge = true;

                if (back_triggered_this_press) {
                    /* BACKは押している間に既に確定済み。離した瞬間には
                     * 何も反応しない(音も鳴らさず、画面遷移も行わない)。 */
                } else {
                    /* 短押しで離した瞬間: 決定操作としてPROCEEDを鳴らす。
                     * sound_hooks_play()(即時割り込み版)を使い、押し始めに
                     * 鳴らしたHITがまだ再生中でもそこで打ち切ってPROCEEDに
                     * 繋げる(HIT→PROCEEDで一つのフレーズになる意図)。 */
                    bool finished = nav_confirm();
                    sound_hooks_play(finished ? UI_SOUND_DONE : UI_SOUND_PROCEED);
                    if (finished) {
                        ESP_LOGI(TAG, "all parameters confirmed - resetting to major category (demo loop)");
                        nav_init();
                    }
                    bsp_display_lock(0);
                    ui_screens_refresh();
                    bsp_display_unlock();
                }
            }
            button_was_pressed = debounced_button_pressed;
        } else {
            ESP_LOGW(TAG, "encoder_poll failed: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}
