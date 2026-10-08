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
 *   P   : 今表示している画面をPNGで保存 (ページ上部の「画面をPNG保存」ボタンと同じ)
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
 *
 * 【高解像度(UI_SCALE=4)と画面のPNG保存】
 * sim/CMakeLists.txtがUI_SCALE=4を定義するため、simは実機の4倍(1280x960)で
 * 描画する(main/ui_screens.cのUI_SCALE参照)。ブラウザ上の表示サイズは
 * CSSで縮小して(SIM_PREVIEW_CSS_W/H)実機と同じ4:3のまま見せる。
 * 「画面をPNG保存」は、いま表示している画面を1280x960のPNGとしてダウンロード
 * する(lv_snapshot_take()で画面オブジェクトを描き直すので、ワイプ演出の
 * 帯や、ページ上のボタン類は写り込まない)。撮りたい状況まで操作してから押す。
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
#include <stdint.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#else
#include <SDL2/SDL.h> // SDL_Delay()用(ネイティブビルド時のみ)
#endif

#ifndef UI_SCALE
#define UI_SCALE 1
#endif

#define SIM_LCD_H_RES (320 * UI_SCALE)
#define SIM_LCD_V_RES (240 * UI_SCALE)

/* ブラウザ上でのプレビュー表示サイズ(CSSピクセル)。描画解像度とは別で、
 * 保存されるPNGのサイズ(SIM_LCD_H_RES x SIM_LCD_V_RES)には影響しない。 */
#define SIM_PREVIEW_CSS_W 640
#define SIM_PREVIEW_CSS_H 480

/* ---- 画面のPNG保存 ---- */

#if defined(__EMSCRIPTEN__)

/* 画面上部にボタンを置き、プレビューのCSSサイズを設定する。ボタンは
 * クリック直後にblurして、キーボードのフォーカスを奪ったままにしない
 * (Enterを押したときボタンが再度クリックされるのを防ぐ)。
 * 保存要求はフラグ(window.__simShotRequested)で受け渡し、C側のメイン
 * ループ(loop_iter)が拾う。 */
EM_JS(void, sim_ui_js_init, (int css_w, int css_h), {
    var cv = Module['canvas'] || document.getElementById('canvas');
    if (cv) {
        cv.style.width = css_w + 'px';
        cv.style.height = css_h + 'px';
    }

    var bar = document.createElement('div');
    bar.style.cssText = 'font:13px sans-serif;padding:6px 8px;background:#e4eefc;' +
        'border-bottom:1px solid #ccc;display:flex;align-items:center;gap:8px;';

    var btn = document.createElement('button');
    btn.textContent = '画面をPNG保存';
    btn.onclick = function() {
        window.__simShotRequested = true;
        btn.blur();
    };

    var hint = document.createElement('span');
    hint.textContent = 'いま表示している画面を高解像度PNGで保存します(Pキーでも保存できます)';

    bar.appendChild(btn);
    bar.appendChild(hint);
    // 音フォルダのバー(sim_sound.cが先頭に挿入済み)の直後に置く
    var ref = document.body.children.length > 1 ? document.body.children[1] : null;
    document.body.insertBefore(bar, ref);
});

EM_JS(int, sim_shot_requested, (), {
    if (window.__simShotRequested) {
        window.__simShotRequested = false;
        return 1;
    }
    return 0;
});

/* data: 画面のXRGB8888ピクセル(メモリ上の並びはB,G,R,X)。キャンバスに
 * RGBAとして描き直してPNG化し、ダウンロードさせる(PNGエンコーダを
 * C側に持たずに済ませるため)。 */
EM_JS(void, sim_save_png_js, (const uint8_t *data, int w, int h, int stride, const char *label), {
    var lbl = UTF8ToString(label);
    var src = HEAPU8.subarray(data, data + stride * h);

    var c = document.createElement('canvas');
    c.width = w;
    c.height = h;
    var ctx = c.getContext('2d');
    var img = ctx.createImageData(w, h);
    var d = img.data;
    for (var y = 0; y < h; y++) {
        var si = y * stride;
        var di = y * w * 4;
        for (var x = 0; x < w; x++, si += 4, di += 4) {
            d[di]     = src[si + 2];
            d[di + 1] = src[si + 1];
            d[di + 2] = src[si];
            d[di + 3] = 255;
        }
    }
    ctx.putImageData(img, 0, 0);

    var t = new Date();
    function p2(n) { return (n < 10 ? '0' : '') + n; }
    var stamp = t.getFullYear() + p2(t.getMonth() + 1) + p2(t.getDate()) + '-' +
                p2(t.getHours()) + p2(t.getMinutes()) + p2(t.getSeconds());

    c.toBlob(function(blob) {
        var a = document.createElement('a');
        a.href = URL.createObjectURL(blob);
        a.download = 'fp_' + lbl + '_' + stamp + '.png';
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        setTimeout(function() { URL.revokeObjectURL(a.href); }, 2000);
    }, 'image/png');
});

#endif /* __EMSCRIPTEN__ */

/* ファイル名に使う、いまの画面の種類。 */
static void build_shot_label(char *out, size_t out_size)
{
    const nav_state_t *st = nav_get_state();
    switch (st->level) {
    case NAV_LEVEL_MAJOR: snprintf(out, out_size, "major"); break;
    case NAV_LEVEL_SUB:   snprintf(out, out_size, "sub");   break;
    case NAV_LEVEL_MENU:  snprintf(out, out_size, "menu");  break;
    case NAV_LEVEL_PARAM:
        if (st->parameter_index >= (int)nav_get_current_menu_item()->parameter_count) {
            snprintf(out, out_size, "param_start");
        } else {
            snprintf(out, out_size, "param%d", st->parameter_index + 1);
        }
        break;
    case NAV_LEVEL_COOKING:
        snprintf(out, out_size, nav_cooking_is_complete() ? "cooking_done" : "cooking");
        break;
    default: snprintf(out, out_size, "screen"); break;
    }
}

static void save_screenshot(void)
{
    char label[32];
    build_shot_label(label, sizeof(label));

    ui_screens_prepare_capture(); /* 点滅中の画面を不透明に戻してから撮る */
    lv_draw_buf_t *buf = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_XRGB8888);
    if (buf == NULL) {
        printf("screenshot failed: lv_snapshot_take returned NULL (memory?)\n");
        return;
    }

#if defined(__EMSCRIPTEN__)
    sim_save_png_js(buf->data, (int)buf->header.w, (int)buf->header.h, (int)buf->header.stride, label);
    printf("screenshot saved: %s (%dx%d)\n", label, (int)buf->header.w, (int)buf->header.h);
#else
    printf("screenshot (%s, %dx%d): PNG保存はブラウザ(Emscripten)ビルドのみ対応\n",
           label, (int)buf->header.w, (int)buf->header.h);
#endif
    lv_draw_buf_destroy(buf);
}

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
    uint32_t key = lv_event_get_key(e);

    /* 画面のPNG保存はワイプ演出中でも受け付ける(撮影は演出の帯を写さず、
     * 下の画面だけを描き直す。音も鳴らさない)。 */
    if (key == 'p' || key == 'P') {
        save_screenshot();
        return;
    }

    if (ui_screens_transition_in_progress()) {
        /* 実機と同様、ワイプ演出中は操作を一切受け付けない。ただし「押しても
         * 何も起きなかった」ことが分かるよう、実機側のような押しっぱなし
         * 長押しキャンセルの再現(app_main.c参照)はキーボード入力には無い
         * ため、ここは単純にdeny音だけ鳴らして無視する。 */
        sound_hooks_play(UI_SOUND_DENY);
        return;
    }

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

#if defined(__EMSCRIPTEN__)
    if (sim_shot_requested()) {
        save_screenshot();
    }
#endif
}

int main(void)
{
    lv_init();

    lv_display_t *disp = lv_sdl_window_create(SIM_LCD_H_RES, SIM_LCD_V_RES);
    lv_sdl_window_set_title(disp, "FoodPrinterMock UI Simulator");

#if UI_SCALE != 1
    /* テーマ(余白/角丸/枠線の太さ)はDPIから寸法を決めるので、実機のDPI(130)の
     * UI_SCALE倍にして、実機の見た目をそのまま拡大した寸法にする。DPIを変えた
     * あとにテーマを初期化し直さないと反映されない。
     * (解像度が大きいとテーマが「大画面向けの余白」へ切り替わってしまう点は、
     *  sim/CMakeLists.txtがLVGLに当てるパッチ patch_lvgl_theme.cmake で
     *  抑えている) */
    lv_display_set_dpi(disp, 130 * UI_SCALE);
    lv_display_set_theme(disp, lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                                                     lv_palette_main(LV_PALETTE_RED),
                                                     LV_THEME_DEFAULT_DARK, LV_FONT_DEFAULT));
#endif

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

#if defined(__EMSCRIPTEN__)
    sim_ui_js_init(SIM_PREVIEW_CSS_W, SIM_PREVIEW_CSS_H);
#endif

    printf("FoodPrinterMock UI simulator: arrows=Encoder move, W/S=Scroll move, "
           "Enter=confirm, Esc/Backspace=back, P=save PNG\n");

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
