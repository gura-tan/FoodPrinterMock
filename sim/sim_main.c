/*
 * main/以下のUIロジック(menu_nav.c / ui_screens.c)をそのままPC/ブラウザ上で動かす
 * ためのエントリポイント。実機のロータリーエンコーダー+ボタンの代わりに
 * キーボードで操作する:
 *   ←/↑ : Encoder側の選択を1つ戻す (delta -1)
 *   →/↓ : Encoder側の選択を1つ進める (delta +1)
 *   W   : Scroll側の選択を1つ戻す (delta -1)
 *   S   : Scroll側の選択を1つ進める (delta +1)
 *   Enter: 決定 (nav_confirm)
 *   Esc / Backspace: キャンセル/戻る (nav_back)
 *
 * 【Unit Scroll対応について】
 * simは実機のような「入力ごとに独立したボタン」を持たない(キーボードには
 * Encoder用/Scroll用の別々の押しボタンが無く、決定/戻るはEnter/Escで共通)。
 * そのためsimが再現するのは「どちらの入力で回転させたかによってMoveCat/
 * MovePrmの音が変わる」という、この試作機の研究テーマの核心部分だけであり、
 * 実機のper-source状態(デバウンス/ジッタガード/長押し判定/おもちゃモードの
 * 累積回転)はsimには存在しない(元々arrow keysにも実装されていなかった)。
 * nav/画面遷移の状態は実機と同じくEncoder/Scrollで共有する。
 *
 * app_main.c(実機版)の該当ループと同じ呼び出し順を踏襲しているが、
 * デバウンス/長押し判定/NVS/SDカードといった実機固有の処理は含めていない
 * (UIの画面遷移だけを確認する用途のため)。音はsim_sound.c(ブラウザで
 * フォルダ選択→Web Audio再生)がsound_hooks.hの実装を差し替えている。
 */
#include "lvgl.h"
#include "src/drivers/sdl/lv_sdl_window.h"
#include "src/drivers/sdl/lv_sdl_mouse.h"
#include "src/drivers/sdl/lv_sdl_keyboard.h"

#include "menu_nav.h"
#include "ui_screens.h"
#include "input_source.h"
#include "sound_hooks.h"

#include <stdio.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#else
#include <SDL2/SDL.h> // SDL_Delay()用(ネイティブビルド時のみ)
#endif

#define SIM_LCD_H_RES 320
#define SIM_LCD_V_RES 240

/* Encoder/Scroll共通の回転処理。実機のprocess_input()と同じ分岐
 * (調理中画面は早送り/巻き戻し、それ以外は選択移動)をキーボード用に
 * 簡略化したもの。音だけがsrcによって変わる(sound_hooks_play_from参照)。 */
static void handle_rotation(int32_t delta, input_source_t src)
{
    if (nav_get_state()->level == NAV_LEVEL_COOKING) {
        /* 調理中画面ではダイヤルは選択移動ではなく残り時間の早送り/
         * 巻き戻し(デモ用)。app_main.c側と同じ割り当て。 */
        bool changed = nav_cooking_adjust(delta);
        bool now_complete = nav_cooking_is_complete();
        sound_hooks_play_from(!changed ? UI_SOUND_DENY : (now_complete ? UI_SOUND_READY : UI_SOUND_MOVE_PARAM), src);
        ui_screens_sync_cooking();
    } else {
        bool moved = nav_move_selection(delta);
        bool on_param_screen = nav_get_state()->level == NAV_LEVEL_PARAM;
        sound_hooks_play_from(moved ? (on_param_screen ? UI_SOUND_MOVE_PARAM : UI_SOUND_MOVE_CATEGORY) : UI_SOUND_DENY, src);
        ui_screens_sync_selection();
    }
}

static void key_event_cb(lv_event_t *e)
{
    if (ui_screens_transition_in_progress()) {
        /* 実機と同様、ワイプ演出中は操作を一切受け付けない。ただし「押しても
         * 何も起きなかった」ことが分かるよう、実機側のような押しっぱなし
         * 長押しキャンセルの再現(app_main.c参照)はキーボード入力には無い
         * ため、ここは単純にdeny音だけ鳴らして無視する。 */
        sound_hooks_play(UI_SOUND_DENY);
        return;
    }

    uint32_t key = lv_event_get_key(e);
    switch (key) {
    case LV_KEY_LEFT:
    case LV_KEY_UP:
        handle_rotation(-1, INPUT_SRC_ENCODER);
        break;
    case LV_KEY_RIGHT:
    case LV_KEY_DOWN:
        handle_rotation(1, INPUT_SRC_ENCODER);
        break;
    case 'w':
    case 'W':
        handle_rotation(-1, INPUT_SRC_SCROLL);
        break;
    case 's':
    case 'S':
        handle_rotation(1, INPUT_SRC_SCROLL);
        break;
    case LV_KEY_ENTER: {
        /* キーボードには実機のような押す/離すの区別が無いため、1回の
         * Enterで「押した瞬間(HIT)→短押しで離した瞬間(PROCEED/DONE)」を
         * 続けて鳴らす(sound_hooks_play()は即時割り込み版なので、HITは
         * すぐPROCEED/DONEに打ち切られてつながる。実機の短押しと同じ)。
         * 決定/戻るはEncoder/Scroll共通の操作として扱う(実機のような
         * per-sourceボタンがキーボードには無いため)ので、常にENCODER扱いで
         * よい(HIT/PROCEED/DONE/BACKは入力によらず共通の音)。
         * nav_confirm()の戻り値=trueは「START確定」の合図であり、状態遷移
         * (調理中画面へ進む/完了確認で大カテゴリへ戻す)はnav_confirm()
         * 自身が内部で行う(main/menu_nav.c参照)。 */
        sound_hooks_play(UI_SOUND_HIT);
        bool finished = nav_confirm();
        if (finished) {
            printf("START confirmed - entering cooking countdown\n");
        }
        sound_hooks_play(finished ? UI_SOUND_DONE : UI_SOUND_PROCEED);
        ui_screens_refresh(false); // 決定操作: ワイプは左->右
        break;
    }
    case LV_KEY_ESC:
    case LV_KEY_BACKSPACE:
        nav_back();
        sound_hooks_play(UI_SOUND_BACK);
        ui_screens_refresh(true); // 戻る操作: ワイプは右->左
        break;
    default:
        break;
    }
}

static void loop_iter(void)
{
    if (nav_get_state()->level == NAV_LEVEL_COOKING) {
        /* 実機側(app_main.c)のポーリングループと同じく、ダイヤル/ボタン
         * 入力の有無に関わらず毎フレーム残り時間を更新する。 */
        if (nav_cooking_tick()) {
            sound_hooks_play(UI_SOUND_READY);
        }
        ui_screens_sync_cooking();
    }
    lv_timer_handler();
}

int main(void)
{
    lv_init();

    lv_display_t *disp = lv_sdl_window_create(SIM_LCD_H_RES, SIM_LCD_V_RES);
    lv_sdl_window_set_title(disp, "FoodPrinterMock UI Simulator");

    lv_indev_t *mouse = lv_sdl_mouse_create();
    (void)mouse; // タッチ相当の入力は使わないが標準構成として作っておく

    lv_indev_t *keyboard = lv_sdl_keyboard_create();
    lv_group_t *group = lv_group_create();
    lv_group_set_default(group);
    lv_indev_set_group(keyboard, group);

    /* どの画面(s_screen/s_param_screen/デバッグ画面)がロードされていても
     * キー入力を受け続けられるよう、lv_layer_top()に常駐する見えないオブジェクト
     * だけをグループに入れてフォーカスを固定する(screen切り替えに影響されない)。 */
    lv_obj_t *input_catcher = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(input_catcher);
    lv_obj_set_size(input_catcher, 0, 0);
    lv_obj_clear_flag(input_catcher, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(input_catcher, LV_OBJ_FLAG_SCROLLABLE);
    lv_group_add_obj(group, input_catcher);
    lv_group_focus_obj(input_catcher);
    lv_obj_add_event_cb(input_catcher, key_event_cb, LV_EVENT_KEY, NULL);

    sound_hooks_init();
    nav_init();
    ui_screens_init();

    printf("FoodPrinterMock UI simulator: arrows=Encoder move, W/S=Scroll move, "
           "Enter=confirm, Esc/Backspace=back\n");

#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop(loop_iter, 0, 1);
#else
    while (1) {
        loop_iter();
        SDL_Delay(5);
    }
#endif

    return 0;
}
