/*
 * main/以下のUIロジック(menu_nav.c / ui_screens.c)をそのままPC/ブラウザ上で動かす
 * ためのエントリポイント。実機のロータリーエンコーダー+ボタンの代わりに
 * キーボードで操作する:
 *   ←/↑ : 選択を1つ戻す (encoder delta -1)
 *   →/↓ : 選択を1つ進める (encoder delta +1)
 *   Enter: 決定 (nav_confirm)
 *   Esc / Backspace: キャンセル/戻る (nav_back)
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
#include "sound_hooks.h"

#include <stdio.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#else
#include <SDL2/SDL.h> // SDL_Delay()用(ネイティブビルド時のみ)
#endif

#define SIM_LCD_H_RES 320
#define SIM_LCD_V_RES 240

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
    case LV_KEY_UP: {
        bool moved = nav_move_selection(-1);
        bool on_param_screen = nav_get_state()->level == NAV_LEVEL_PARAM;
        sound_hooks_play(moved ? (on_param_screen ? UI_SOUND_MOVE_PARAM : UI_SOUND_MOVE_CATEGORY) : UI_SOUND_DENY);
        ui_screens_sync_selection();
        break;
    }
    case LV_KEY_RIGHT:
    case LV_KEY_DOWN: {
        bool moved = nav_move_selection(1);
        bool on_param_screen = nav_get_state()->level == NAV_LEVEL_PARAM;
        sound_hooks_play(moved ? (on_param_screen ? UI_SOUND_MOVE_PARAM : UI_SOUND_MOVE_CATEGORY) : UI_SOUND_DENY);
        ui_screens_sync_selection();
        break;
    }
    case LV_KEY_ENTER: {
        /* キーボードには実機のような押す/離すの区別が無いため、1回の
         * Enterで「押した瞬間(HIT)→短押しで離した瞬間(PROCEED/DONE)」を
         * 続けて鳴らす(sound_hooks_play()は即時割り込み版なので、HITは
         * すぐPROCEED/DONEに打ち切られてつながる。実機の短押しと同じ)。 */
        sound_hooks_play(UI_SOUND_HIT);
        bool finished = nav_confirm();
        if (finished) {
            printf("all parameters confirmed - resetting to major category (demo loop)\n");
            nav_init();
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

    printf("FoodPrinterMock UI simulator: arrows=move, Enter=confirm, Esc/Backspace=back\n");

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
