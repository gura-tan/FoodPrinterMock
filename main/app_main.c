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
#include "input_source.h"
#include "input_hub.h"
#include "encoder_unit.h"    // ENCODER_REGISTER_SCAN_MODE用(encoder_debug_dump_registers)
#include "scroll_unit.h"     // SCROLL_REGISTER_SCAN_MODE用(scroll_debug_dump_registers)
#include "sound_hooks.h"
#include "sd_storage.h"
#include "debug_preset.h"

static const char *TAG = "app_main";

/* 操作入力はEncoder(Port.A)とScroll(Port.B)の2系統。どちらも「回転+押し込み
 * ボタン」で、下記のボタン/長押し/おもちゃモード等の判定は入力ごとに独立して
 * 行う(process_input()、input_state_t参照)。nav(画面遷移の状態)、おもちゃ
 * モードのON/OFF、画面遷移演出の向きだけは2系統で共有する。 */

/* 「決定」操作: 入力のボタンを押してすぐ(CANCEL_HOLD_MS未満で)離した場合
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
 * 「同じ入力のボタン」がBOOT_HOLD_SAMPLE_COUNT回連続で押下状態と確認できた
 * 場合だけデバッグモードへ入る(Encoder/Scrollのどちらか一方で満たせばよい。
 * 合計 (COUNT-1)*INTERVAL_MS 程度の間、押され続けている必要がある。
 * 入力をまたいで交互に押されても成立しない)。 */
#define BOOT_HOLD_SAMPLE_COUNT        5
#define BOOT_HOLD_SAMPLE_INTERVAL_MS 30

/* 【おもちゃモード】ボタンを押しながらダイヤルを回すとトグルする。
 * 画面には何も描画しない(バックライトを消灯するだけ)が、nav/soundの
 * 処理自体は通常時と完全に同じまま動かし続ける(操作を受け付け、対応する
 * 音を鳴らす)。押している間の累積回転量がこの数値に達した時点で
 * 離す操作を待たずに確定させ、以後同じ押下中は再トグルしない
 * (CANCEL_HOLD_MSの「戻る」長押しとは独立した別ジェスチャーとして扱う:
 * 確定した場合はback_triggered_this_pressを立てて「戻る」長押し判定と
 * 離した瞬間のPROCEED確定の両方を抑止する)。
 * 累積は入力ごとに数える(Encoderを押しながらScrollを回しても成立しない)。 */
#define TOY_MODE_ROTATE_STEPS  4

/* デバッグ用プリセット選択画面の一覧先頭に置く固定選択肢。選ぶとNVSの
 * デバッグ上書き設定を消去し、通常のSD preset.txt / defaultの挙動に戻す。 */
static const char *const DEBUG_PICKER_RESET_LABEL = "(SDのpreset.txtに戻す)";

/* 【診断モード】1にすると通常のnav処理を行わず、レジスタの生値をログに
 * 出し続けるだけになる。エンコーダーのレジスタ配置が未確認のため、
 * ダイヤルを回しながらどのバイトが変化するかを目視で特定するために使う。
 * 使い方: 1にしてビルド・書き込み → シリアルモニタを見ながらダイヤルを
 * ゆっくり回す/ボタンを押す → 変化したレジスタのアドレスと挙動を教えてほしい。 */
#define ENCODER_REGISTER_SCAN_MODE  0

/* 【診断モード(Unit Scroll)】ENCODER_REGISTER_SCAN_MODEのScroll版。
 * Scrollのレジスタ配置(scroll_unit.hの「未検証事項」)は未確認のため、
 * 1にして書き込み、Scrollをゆっくり回す/ボタンを押しながらシリアルモニタを見て、
 * どのレジスタのどのバイトがどう変化するか(1ノッチあたりの増分、ボタン押下時の値、
 * 回転方向で増減のどちらか)を確認する。
 * 0x50以降(リセット用レジスタの可能性)は副作用を避けるため読まない。
 * バス自体が初期化されていれば、カウンタの読み出しに失敗している状態でも
 * ダンプできる(配置が違うときこそ使う)。 */
#define SCROLL_REGISTER_SCAN_MODE   0

/* ---- 入力ごとの状態 ----
 * 1入力=1つ持つ。ボタンのデバウンス/ジッタガード/長押し判定は「その入力の
 * ボタン」に対するものなので、2系統を混ぜると互いの状態を壊す(例: Encoderの
 * 長押し中にScrollが動くと、共有のbutton_was_pressedがずれる)ため分けてある。 */
typedef struct {
    bool      button_was_pressed;
    TickType_t press_started_tick;
    TickType_t last_button_edge_tick;
    bool      have_button_edge;
    /* 押している間にCANCEL_HOLD_MSへ達してBACKを確定させた(または
     * おもちゃモードのトグルを確定させた)かどうか。確定後、実際にボタンを
     * 離した瞬間の処理を無効化するために使う。 */
    bool      back_triggered_this_press;
    /* 「戻る」演出の最中だけ使う、ボタンの押下状態の影武者。
     * button_was_pressed本体(長押し/デバウンス判定に使う本来の状態)は
     * 演出中は一切更新せず凍結したままにしたいので、deny音を鳴らす
     * 判定専用にこちらで別途追跡する。 */
    bool      deny_shadow_pressed;
    /* deny_shadow_pressedを最後に初期化した時点のs_transition_epoch。
     * 画面遷移のたびに(refresh_screen()がepochを進めるので)、その遷移の
     * 最初のポーリングで影武者をbutton_was_pressedから初期化し直す。
     * 「戻る」を確定させた押下(引き続き押されている)を新しい押下と
     * みなしてdenyを鳴らさないための初期化で、他方の入力のボタンが
     * 遷移開始時にすでに押されていた場合も同じ扱いになる。 */
    uint32_t  shadow_epoch;
    /* 【おもちゃモード】押下中の累積回転量と、今の押下で既にトグル済みか
     * (1回の押下で何度もトグルしないため)。 */
    int32_t   toy_mode_rotate_accum;
    bool      toy_mode_gesture_consumed_this_press;
} input_state_t;

static input_state_t s_input_state[INPUT_SRC_COUNT];

/* 2系統で共有する状態 */
static bool     s_toy_mode_active;
static bool     s_transition_is_back;   // 今再生中(または直前に開始した)画面遷移が「戻る」方向か
static uint32_t s_transition_epoch;     // 画面遷移を開始するたびに進める(input_state_t.shadow_epoch参照)

/* 画面遷移(ui_screens_refresh)を開始する。「戻る」方向かどうかの記録と、
 * 影武者を初期化し直すための世代番号の更新を、ui_screens_refresh()を呼ぶ
 * 箇所と同じタイミングで必ず行うための窓口。 */
static void refresh_screen(bool is_back)
{
    s_transition_is_back = is_back;
    s_transition_epoch++;
    bsp_display_lock(0);
    ui_screens_refresh(is_back);
    bsp_display_unlock();
}

/* 利用可能な全入力を1回ずつポーリングする。回転は合計、ボタンは論理OR
 * (デバッグ選択画面のように、どの入力から操作してもよい場面用)。
 * 1つでも応答があればtrue。全て未接続/通信エラーならfalse(出力は0/false)。
 * out_deltaがNULLのときは回転量は読み捨てる。 */
static bool poll_all_inputs(int32_t *out_delta, bool *out_pressed)
{
    int32_t delta_sum = 0;
    bool pressed_any = false;
    bool any_ok = false;

    for (int i = 0; i < (int)INPUT_SRC_COUNT; i++) {
        input_source_t src = (input_source_t)i;
        if (!input_hub_is_available(src)) {
            continue;
        }
        int32_t delta = 0;
        bool pressed = false;
        if (input_hub_poll(src, out_delta ? &delta : NULL, &pressed) != ESP_OK) {
            continue;
        }
        any_ok = true;
        delta_sum += delta;
        pressed_any = pressed_any || pressed;
    }

    if (out_delta) {
        *out_delta = delta_sum;
    }
    if (out_pressed) {
        *out_pressed = pressed_any;
    }
    return any_ok;
}

/* ボタンが離されるまでブロックして待つ(デバッグ選択画面まわりの入力を
 * 必ず「離されている」状態から開始・終了させるためのヘルパー)。
 * どの入力もポーリングできなくなった場合(エンコーダーが外れた等)は
 * 無限待ちにならないよう、その時点で即座に抜ける。 */
static void wait_for_button_release(void)
{
    for (;;) {
        bool pressed = false;
        if (!poll_all_inputs(NULL, &pressed) || !pressed) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

/* デバッグ用プリセット選択画面を表示し、選択が確定したらNVSに保存して
 * esp_restart()する(正常系ではこの関数は戻らない)。
 * SDカード上に有効なプリセットフォルダが1つも見つからなかった場合だけ、
 * 何もせずに戻る(呼び出し側は通常起動にフォールスルーする)。
 * 操作はEncoder/Scrollのどちらでもよい(回転は合計、ボタンは論理OR)。
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
        if (!poll_all_inputs(&delta, &button_pressed)) {
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

/* 入力1つぶんの処理(ポーリング→デバウンス→操作の解釈→音とnavへの反映)。
 * 以前のapp_mainのループ本体を、入力ごとの状態(input_state_t)を引数にして
 * 切り出したもの。処理内容は入力によらず同じで、違うのは
 *  - MOVE音がどの入力の音になるか(sound_hooks_play_from()のsrc)
 *  - 状態(s_input_state[src])だけ。
 * 初期化に失敗した(未接続の)入力は何もせず戻る。通信エラーのログは
 * input_hub側が状態変化時だけ出すので、ここでは何も出さない。 */
static void process_input(input_source_t src)
{
    if (!input_hub_is_available(src)) {
        return;
    }
    input_state_t *st = &s_input_state[src];

    if (ui_screens_transition_in_progress()) {
        /* 画面遷移のワイプ演出中は、ダイヤル操作や決定/戻る操作を
         * 一切反映しない。ただし「操作しても変化が起きない」ことを
         * 音で伝えるため、ポーリング自体は続けて検知だけ行う
         * (演出はTRANSITION_COVER_MS+HOLD_MS+REVEAL_MSの合計で
         * 470ms前後かかる)。
         *
         * ダイヤル回転: 演出中に検知した回転量はその場で読み捨てる
         * (=本当に無効化する)。
         *
         * ボタン: 「戻る」演出(長押しでnav_back()を確定させた直後の
         * ワイプ)の最中に限り、押し直しの立ち上がりエッジをdeny音の
         * 対象にする(deny_shadow_pressedで影武者的に追跡するだけで、
         * button_was_pressed等の本来の状態には触れない)。
         * 「決定」演出(PROCEED/DONE)の最中の押下は、従来通り一切
         * 素通りさせる: 短押し直後に押しっぱなしにすると演出終了後に
         * 新規の押下として検出され、そのまま長押しでBACKに確定する
         * 「押し間違いにすぐ気付いて戻れる」挙動が実機確認で好まれて
         * いるため、これを崩さないようにしている。 */
        if (st->shadow_epoch != s_transition_epoch) {
            st->shadow_epoch = s_transition_epoch;
            st->deny_shadow_pressed = st->button_was_pressed; // 遷移開始時点で押されていたものは新しい押下ではない
        }

        int32_t delta = 0;
        bool button_pressed = false;
        esp_err_t err = input_hub_poll(src, &delta, s_transition_is_back ? &button_pressed : NULL);
        if (err == ESP_OK) {
            bool deny = (delta != 0);
            if (s_transition_is_back) {
                if (button_pressed && !st->deny_shadow_pressed) {
                    deny = true;
                }
                st->deny_shadow_pressed = button_pressed;
            }
            if (deny) {
                sound_hooks_play(UI_SOUND_DENY);
            }
        }
        return;
    }

    int32_t delta = 0;
    bool button_pressed = false;
    esp_err_t err = input_hub_poll(src, &delta, &button_pressed);
    if (err != ESP_OK) {
        return;
    }

    TickType_t now = xTaskGetTickCount();
    bool within_jitter_guard =
        st->have_button_edge &&
        ((now - st->last_button_edge_tick) * portTICK_PERIOD_MS < BUTTON_JITTER_GUARD_MS);

    /* 直前に確定したボタンエッジからBUTTON_DEBOUNCE_MS未満しか
     * 経っていない場合、生の読み値が逆転していても接点バウンスと
     * みなして無視し、直前の確定状態(button_was_pressed)を使う。 */
    bool within_button_debounce =
        st->have_button_edge &&
        ((now - st->last_button_edge_tick) * portTICK_PERIOD_MS < BUTTON_DEBOUNCE_MS);
    bool debounced_button_pressed = within_button_debounce ? st->button_was_pressed : button_pressed;

    /* 【おもちゃモード】ボタンが(前回ポーリング時点から引き続き)
     * 押されている間のダイヤル回転は、通常の選択移動/早送りには
     * 使わずおもちゃモードのトグル判定に回す。押し始めた直後の
     * within_jitter_guard期間は従来通り無視する。 */
    bool button_held_from_before = debounced_button_pressed && st->button_was_pressed;
    if (button_held_from_before && !st->back_triggered_this_press &&
        !st->toy_mode_gesture_consumed_this_press && delta != 0 && !within_jitter_guard) {
        st->toy_mode_rotate_accum += (int32_t)delta;
        if (st->toy_mode_rotate_accum >= TOY_MODE_ROTATE_STEPS ||
            st->toy_mode_rotate_accum <= -TOY_MODE_ROTATE_STEPS) {
            s_toy_mode_active = !s_toy_mode_active;
            st->toy_mode_gesture_consumed_this_press = true;
            st->back_triggered_this_press = true; // 通常の長押しBACK/PROCEEDを抑止
            /* バックライト消灯だけでは(パネルの特性上)うっすら表示が
             * 透けて見えてしまったため、黒いオーバーレイで完全に覆う
             * (ui_screens_set_toy_mode()参照)。バックライト自体も
             * 消灯しておくのは省電力目的の付随的なもの。 */
            bsp_display_lock(0);
            ui_screens_set_toy_mode(s_toy_mode_active);
            bsp_display_unlock();
            if (s_toy_mode_active) {
                bsp_display_backlight_off();
            } else {
                bsp_display_backlight_on();
            }
            sound_hooks_play(UI_SOUND_DONE);
        }
    } else if (delta != 0 && !within_jitter_guard && nav_get_state()->level == NAV_LEVEL_COOKING) {
        /* 調理中画面ではダイヤルは選択移動ではなく残り時間の早送り/
         * 巻き戻し(デモ用)。完了後はnav_cooking_adjust()側で常に
         * 無効化されるので、ここではchangedの有無だけ見ればよい。 */
        bool changed = nav_cooking_adjust(delta);
        bool now_complete = nav_cooking_is_complete();
        if (now_complete) {
            sound_hooks_play_from(!changed ? UI_SOUND_DENY : (now_complete ? UI_SOUND_READY : UI_SOUND_MOVE_PARAM), src);
        } //三項演算子の最適な処理が分からなかったので仮。タイマー中の操作は音が鳴らないように
        bsp_display_lock(0);
        ui_screens_sync_cooking();
        bsp_display_unlock();
    } else if (delta != 0 && !within_jitter_guard) {
        /* 既にリストの端にいて、さらに同方向へ回した場合は選択が
         * 変化しないので、MOVEの代わりにDENYを鳴らして「これ以上は
         * 進めない」ことを伝える。 */
        bool moved = nav_move_selection(delta);
        /* PROCEED/DONE再生中でも即座に打ち切って割り込む。実機で
         * 試した結果、MOVE/HITがPROCEEDのテールを打ち切ってでも
         * 常に即座に反応したほうが操作感が安定するため。
         * MOVE音は操作した入力(src)ごとに別の音を鳴らせる。 */
        bool on_param_screen = nav_get_state()->level == NAV_LEVEL_PARAM;
        ui_sound_id_t move_sound = on_param_screen ? UI_SOUND_MOVE_PARAM : UI_SOUND_MOVE_CATEGORY;
        sound_hooks_play_from(moved ? move_sound : UI_SOUND_DENY, src);
        bsp_display_lock(0);
        ui_screens_sync_selection();
        bsp_display_unlock();
    } else if (delta != 0 && within_jitter_guard) {
        ESP_LOGD(TAG, "ignoring delta=%ld near a button edge (jitter guard, %s)",
                 (long)delta, input_hub_name(src));
    }

    if (debounced_button_pressed && !st->button_was_pressed) {
        /* 押し始め: 画面遷移とは独立して、まず音だけ鳴らす */
        sound_hooks_play(UI_SOUND_HIT);
        st->press_started_tick = now;
        st->last_button_edge_tick = st->press_started_tick;
        st->have_button_edge = true;
        st->back_triggered_this_press = false;
        st->toy_mode_rotate_accum = 0;
        st->toy_mode_gesture_consumed_this_press = false;
    } else if (debounced_button_pressed && st->button_was_pressed && !st->back_triggered_this_press) {
        /* 押され続けている間: 長押し時間(CANCEL_HOLD_MS)に達した瞬間、
         * 離す操作を待たずにBACKを確定させる */
        TickType_t held_ms = (now - st->press_started_tick) * portTICK_PERIOD_MS;
        if (held_ms >= CANCEL_HOLD_MS) {
            nav_back();
            sound_hooks_play(UI_SOUND_BACK);
            st->back_triggered_this_press = true;
            /* 確定させた押下は引き続き押されている(遷移中の影武者は、
             * 次のポーリングでbutton_was_pressed=trueから初期化される) */
            refresh_screen(true); // 戻る操作: ワイプは右→左
        }
    } else if (!debounced_button_pressed && st->button_was_pressed) {
        st->last_button_edge_tick = now;
        st->have_button_edge = true;

        if (st->back_triggered_this_press) {
            /* BACKは押している間に既に確定済み。離した瞬間には
             * 何も反応しない(音も鳴らさず、画面遷移も行わない)。 */
        } else {
            /* 短押しで離した瞬間: 決定操作としてPROCEEDを鳴らす。
             * sound_hooks_play()(即時割り込み版)を使い、押し始めに
             * 鳴らしたHITがまだ再生中でもそこで打ち切ってPROCEEDに
             * 繋げる(HIT→PROCEEDで一つのフレーズになる意図)。 */
            /* nav_confirm()の戻り値=true は「START確定」の合図であり、
             * 状態遷移(調理中画面へ進む/完了確認で大カテゴリへ戻す)は
             * nav_confirm()自身が内部で行うので、ここでは呼び出し側は
             * DONE音を鳴らすだけでよい(以前あったnav_init()の呼び出しは
             * 不要になった: 今それをやると調理中画面への遷移を
             * 即座に上書きしてしまう)。 */
            bool finished = nav_confirm();
            sound_hooks_play(finished ? UI_SOUND_DONE : UI_SOUND_PROCEED);
            if (finished) {
                ESP_LOGI(TAG, "START confirmed - entering cooking countdown");
            }
            refresh_screen(false); // 決定操作: ワイプは左→右
        }
    }
    st->button_was_pressed = debounced_button_pressed;
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

    /* 入力デバイス(Encoder/Scroll)は未接続でも起動を継続できるようにする
     * (以前はESP_ERROR_CHECKで即abort→再起動ループしていた)。
     * input_hub_poll()は初期化に失敗した入力に対してESP_ERR_INVALID_STATEを
     * 返すだけで、以降の処理(process_input/poll_all_inputs)はその入力を
     * 黙ってスキップする。
     * 【デバッグ用プリセット選択】起動時ボタン長押し検出にも使うため、
     * SDマウント・ディスプレイ開始より前に初期化する。エンコーダーは
     * GPIO1/2の専用I2Cバス、ScrollはGPIO8/9のソフトI2Cを使い、どちらも
     * LCD/SDが共有するGPIO35とは無関係なため、この位置でも問題ない。 */
    esp_err_t input_err = input_hub_init();
    if (input_err != ESP_OK) {
        ESP_LOGW(TAG, "利用できる入力デバイスがありません(EncoderもScrollも未接続の可能性)。"
                      "切り分けのため起動は継続します");
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

#if SCROLL_REGISTER_SCAN_MODE
    ESP_LOGW(TAG, "SCROLL_REGISTER_SCAN_MODE=1: レジスタダンプのみ実行し、UIは初期化しません");
    while (1) {
        /* Encoderで判明した配置(0x10=カウンタ, 0x20=ボタン)を中心に、隣接領域も
         * 見る。0x50以降は読まない(リセット用レジスタの可能性があるため)。 */
        scroll_debug_dump_registers(0x00, 8);
        scroll_debug_dump_registers(0x10, 8);
        scroll_debug_dump_registers(0x20, 4);
        scroll_debug_dump_registers(0x30, 4);
        scroll_debug_dump_registers(0x40, 4);
        vTaskDelay(pdMS_TO_TICKS(300));
    }
#endif

    /* 【デバッグ用プリセット選択】起動直後にボタンが押されていたら、通常の
     * メニューではなくSDカード上のプリセット選択画面に入る。誤検出を
     * 避けるため、同じ入力のボタンが複数回連続で押下確認できた場合だけ
     * デバッグモードへ入る(Encoder/Scrollのどちらでもよい)。 */
    bool enter_debug_mode = false;
    if (input_err == ESP_OK) {
        int held_samples[INPUT_SRC_COUNT] = {0};
        for (int i = 0; i < BOOT_HOLD_SAMPLE_COUNT; i++) {
            bool any_still_held = false;
            for (int s = 0; s < (int)INPUT_SRC_COUNT; s++) {
                bool pressed = false;
                if (input_hub_poll((input_source_t)s, NULL, &pressed) != ESP_OK || !pressed) {
                    held_samples[s] = 0;
                    continue;
                }
                held_samples[s]++;
                if (held_samples[s] == i + 1) {
                    any_still_held = true; // この入力は最初のサンプルから連続で押されている
                }
            }
            if (!any_still_held) {
                break; // 誰も押していない(通常起動)ならすぐ抜ける
            }
            if (i + 1 < BOOT_HOLD_SAMPLE_COUNT) {
                vTaskDelay(pdMS_TO_TICKS(BOOT_HOLD_SAMPLE_INTERVAL_MS));
            }
        }
        for (int s = 0; s < (int)INPUT_SRC_COUNT; s++) {
            if (held_samples[s] >= BOOT_HOLD_SAMPLE_COUNT) {
                enter_debug_mode = true;
            }
        }
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

    while (1) {
        if (!ui_screens_transition_in_progress() && nav_get_state()->level == NAV_LEVEL_COOKING) {
            /* 調理中画面のカウントダウンは実時間で進むため、ダイヤル/ボタン
             * 入力の有無に関わらず毎ループ更新する。0に達した瞬間だけ
             * nav_cooking_tick()がtrueを返すので、そこでreadyサウンドを鳴らす。
             * (画面遷移演出中は従来どおり更新しない) */
            if (nav_cooking_tick()) {
                sound_hooks_play(UI_SOUND_READY);
            }
            bsp_display_lock(0);
            ui_screens_sync_cooking();
            bsp_display_unlock();
        }

        /* Encoder→Scrollの順に、毎周期それぞれ1回ずつ処理する。
         * 1周期の所要時間はPOLL_INTERVAL_MSに、Scrollのソフト
         * I2C(1回あたり1〜2ms程度の見込み)とEncoderのI2C分が上乗せされる。 */
        for (int s = 0; s < (int)INPUT_SRC_COUNT; s++) {
            process_input((input_source_t)s);
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}
