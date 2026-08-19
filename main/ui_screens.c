#include "ui_screens.h"
#include "menu_data.h"
#include "lvgl.h"
#include "fonts/font_ja_menu_14.h"
#include "icons/icon_salt.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

/*
 * SquareLine Studioは使わず、LVGLのC APIを直接呼んで組んでいる。
 *
 * 注意(要確認): lv_scr_load() / lv_screen_load() や一部のスタイルAPIは
 * LVGLのバージョン(v8系 / v9系)で名前が異なることがある。BSPが依存している
 * LVGLのバージョンを managed_components/lvgl/library.json 等で確認し、
 * 差異があれば適宜読み替えてほしい(本コードはv8.3〜v9.2あたりの
 * 共通APIを想定して書いている)。
 */

static const char *TAG = "ui_screens";

static lv_obj_t *s_screen;
static lv_obj_t *s_title_label;
static lv_obj_t *s_roller;

static const char *level_title(nav_level_t level)
{
    switch (level) {
        case NAV_LEVEL_MAJOR: return "大カテゴリ";
        case NAV_LEVEL_SUB:   return "小カテゴリ";
        case NAV_LEVEL_MENU:  return "メニュー選択";
        case NAV_LEVEL_PARAM: return "パラメータ調整";
    }
    return "";
}

static void build_roller_options_string(char *buf, size_t buf_size)
{
    size_t count = 0;
    const char *const *opts = nav_get_current_options(&count);
    buf[0] = '\0';
    for (size_t i = 0; i < count; i++) {
        strncat(buf, opts[i], buf_size - strlen(buf) - 1);
        if (i + 1 < count) {
            strncat(buf, "\n", buf_size - strlen(buf) - 1);
        }
    }
}

/* ---- パラメータ調整画面(円ゲージ) ----
 * NAV_LEVEL_PARAMだけは他の3階層と違う専用画面を使う。
 * 「量」「塩味」+「START」を横並びの円(lv_arc)で表示し、ダイヤルで操作中の
 * パラメータの円だけアクセントカラーにする(main/../デザインスケッチ-
 * パラメーター.png参照)。円の見た目:
 *   - パラメータの円: 背景(LV_PART_MAIN)は非表示にし、インジケータ
 *     (LV_PART_INDICATOR)だけを12時ちょうどから表示 → 値が増えるほど
 *     時計回りにリングが伸び、maxで隙間なく1周する(bg_angle_endに
 *     start+360を明示的に渡すと、lv_arc内部のラップアラウンド判定
 *     (end<startのときだけ+360する)を素通りしてそのまま360°分の
 *     スイープ幅として使われる。デフォルトの(135,45)も内部的には
 *     135→405に補正されて使われており、この仕組み自体はライブラリの
 *     標準動作)
 *   - START の円: 常に満円のリング(値の概念を持たない仮想ステップ)
 * 塩味だけは数値の代わりにアイコン(icon_salt, main/icons/参照)を中央に置く。
 * アイコンはA8(アルファのみ)フォーマットなのでimage_recolorスタイルの色が
 * そのまま塗り色になり、円のアクセントカラーと自動的に揃う。 */

#define GAUGE_SCREEN_W        320   // BSP_LCD_H_RES (main/CMakeLists.txtが依存するm5stack_core_s3のBSP値と一致)
#define GAUGE_MAX_SLOTS         4   // パラメータ数+1(START)の実用上の上限。320px幅で窮屈にならない範囲
/* 320px幅を3分割(約106px/枠)した中に収める円の直径。線幅の分だけ見た目の
 * 外接円はこれより一回り大きくなるので、隣同士がくっつかない値にしてある
 * (実機で見て詰まって見えるようなら調整すること)。 */
#define GAUGE_DIAMETER         88
#define GAUGE_ARC_WIDTH         8
#define GAUGE_Y_OFFSET         14   // タイトルの下に少し余裕を持たせる
#define GAUGE_ANGLE_BG_START  270   // 12時ちょうど(0deg=3時, 90deg=6時, 180deg=9時, 270deg=12時)
#define GAUGE_ANGLE_BG_END    (GAUGE_ANGLE_BG_START + 360) // 隙間なく時計回りに1周させる
#define GAUGE_COLOR_ACTIVE  0x3399ff  // 操作中の円のアクセントカラー(スケッチのSTARTの青)
#define GAUGE_COLOR_INACTIVE 0x606060 // 操作対象外の円のグレー

/* 塩味パラメータだけアイコン表示にする対応づけ。将来アイコン付きパラメータが
 * 増えたらparameter_def_tにアイコン参照フィールドを足す形に切り替えてよいが、
 * 試作段階では名前で判定するだけに留めている。 */
#define ICON_PARAM_NAME_SALT "塩味"

typedef struct {
    lv_obj_t *circle;      // lv_arc本体
    lv_obj_t *label;       // 数値/STARTラベル(アイコン表示のスロットではNULL)
    lv_obj_t *icon;        // アイコン画像(数値表示のスロットではNULL)
    bool      is_start;
    int       param_index; // is_start==trueのときは-1
} gauge_slot_t;

static lv_obj_t     *s_param_screen;
static lv_obj_t     *s_param_title_label;
static gauge_slot_t  s_gauge_slots[GAUGE_MAX_SLOTS];
static int           s_gauge_slot_count;
static const menu_item_def_t *s_gauge_built_for; // 直近にbuild_param_gauges()した対象(メニュー切り替え検出用)

static void build_param_gauges(const menu_item_def_t *menu)
{
    lv_obj_clean(s_param_screen); // 既存の子(前回のタイトル/円)を全部消してから作り直す
    memset(s_gauge_slots, 0, sizeof(s_gauge_slots));

    s_param_title_label = lv_label_create(s_param_screen);
    lv_obj_set_style_text_color(s_param_title_label, lv_color_hex(0xffffff), 0);
    lv_obj_align(s_param_title_label, LV_ALIGN_TOP_MID, 0, 10);
    lv_label_set_text(s_param_title_label, level_title(NAV_LEVEL_PARAM));

    size_t param_count = menu->parameter_count;
    if (param_count > (size_t)(GAUGE_MAX_SLOTS - 1)) {
        /* STARTの1枠は必ず確保し、超過分のパラメータだけを切り詰める */
        ESP_LOGW(TAG, "parameter_count=%u はゲージ画面の上限(%d個, STARTの分を含む)を超えるため切り詰めます",
                 (unsigned)param_count, GAUGE_MAX_SLOTS);
        param_count = GAUGE_MAX_SLOTS - 1;
    }
    size_t slot_count = param_count + 1; // +1 = START
    s_gauge_slot_count = (int)slot_count;

    int32_t slot_w = GAUGE_SCREEN_W / (int32_t)slot_count;
    for (int i = 0; i < (int)slot_count; i++) {
        bool is_start = (i >= (int)param_count);
        int32_t x = i * slot_w + slot_w / 2 - GAUGE_SCREEN_W / 2;

        lv_obj_t *circle = lv_arc_create(s_param_screen);
        lv_obj_set_size(circle, GAUGE_DIAMETER, GAUGE_DIAMETER);
        lv_obj_align(circle, LV_ALIGN_CENTER, x, GAUGE_Y_OFFSET);
        /* 試作0はエンコーダーのみで操作する方針のため、rollerと同じく
         * タッチでの直接操作(ドラッグでの値変更)は無効化する */
        lv_obj_clear_flag(circle, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(circle, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_border_width(circle, 0, LV_PART_KNOB);

        s_gauge_slots[i].circle = circle;
        s_gauge_slots[i].is_start = is_start;
        s_gauge_slots[i].param_index = is_start ? -1 : i;

        if (is_start) {
            lv_arc_set_bg_angles(circle, 0, 360); // 隙間の無い満円
            lv_obj_set_style_arc_opa(circle, LV_OPA_TRANSP, LV_PART_INDICATOR); // 値の概念が無いので非表示
            lv_obj_set_style_arc_width(circle, GAUGE_ARC_WIDTH, LV_PART_MAIN);
            lv_obj_set_style_arc_rounded(circle, false, LV_PART_MAIN);

            lv_obj_t *label = lv_label_create(circle);
            lv_label_set_text(label, "START");
            lv_obj_center(label);
            s_gauge_slots[i].label = label;
        } else {
            const parameter_def_t *p = &menu->parameters[i];
            lv_arc_set_bg_angles(circle, GAUGE_ANGLE_BG_START, GAUGE_ANGLE_BG_END);
            lv_obj_set_style_arc_opa(circle, LV_OPA_TRANSP, LV_PART_MAIN); // 背景トラックは非表示、値の分だけリングが伸びる
            lv_obj_set_style_arc_width(circle, GAUGE_ARC_WIDTH, LV_PART_INDICATOR);
            lv_obj_set_style_arc_rounded(circle, false, LV_PART_INDICATOR);
            lv_arc_set_range(circle, p->min_value, p->max_value);

            if (strcmp(p->name, ICON_PARAM_NAME_SALT) == 0) {
                lv_obj_t *icon = lv_image_create(circle);
                lv_image_set_src(icon, &icon_salt);
                lv_obj_center(icon);
                s_gauge_slots[i].icon = icon;
            } else {
                lv_obj_t *label = lv_label_create(circle);
                lv_obj_center(label);
                s_gauge_slots[i].label = label;
            }
        }
    }

    s_gauge_built_for = menu;
}

static void sync_param_gauges(void)
{
    const nav_state_t *state = nav_get_state();
    int active_index = (state->level == NAV_LEVEL_PARAM) ? state->parameter_index : -1;

    for (int i = 0; i < s_gauge_slot_count; i++) {
        gauge_slot_t *slot = &s_gauge_slots[i];
        bool active = (i == active_index);
        lv_color_t color = lv_color_hex(active ? GAUGE_COLOR_ACTIVE : GAUGE_COLOR_INACTIVE);

        if (slot->is_start) {
            lv_obj_set_style_arc_color(slot->circle, color, LV_PART_MAIN);
            lv_obj_set_style_text_color(slot->label, color, 0);
            continue;
        }

        int32_t value = nav_get_param_live_value(slot->param_index);
        lv_obj_set_style_arc_color(slot->circle, color, LV_PART_INDICATOR);
        lv_arc_set_value(slot->circle, value);

        if (slot->label) {
            const parameter_def_t *p = &nav_get_current_menu_item()->parameters[slot->param_index];
            char buf[24];
            snprintf(buf, sizeof(buf), "%ld%s", (long)value, p->unit);
            lv_label_set_text(slot->label, buf);
            lv_obj_set_style_text_color(slot->label, color, 0);
        }
        if (slot->icon) {
            /* A8(アルファのみ)画像はimage_recolorの色がそのまま塗り色になる
             * (image_recolor_opaの指定は不要。LVGLのA8描画パスが常にrecolorを
             * 直接使う実装のため) */
            lv_obj_set_style_image_recolor(slot->icon, color, 0);
        }
    }
}

static void show_param_gauges(void)
{
    const menu_item_def_t *menu = nav_get_current_menu_item();
    if (menu != s_gauge_built_for) {
        build_param_gauges(menu);
    }
    sync_param_gauges();
    lv_scr_load(s_param_screen);
}

void ui_screens_init(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x101010), 0);
    /* ルートに設定すればスタイル継承でタイトルラベル・rollerの項目テキスト
     * 双方に効く。デフォルトの CONFIG_LV_FONT_SOURCE_HAN_SANS_SC_14_CJK は
     * 簡体字中国語向けの字形セットで「揚」「麺」等が欠けていたための対応
     * (詳細は main/fonts/font_ja_menu_14.h 参照)。 */
    lv_obj_set_style_text_font(s_screen, &font_ja_menu_14, 0);

    s_title_label = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_title_label, lv_color_hex(0xffffff), 0);
    lv_obj_align(s_title_label, LV_ALIGN_TOP_MID, 0, 10);

    s_roller = lv_roller_create(s_screen);
    lv_roller_set_visible_row_count(s_roller, 4);
    lv_obj_set_width(s_roller, 260);
    lv_obj_align(s_roller, LV_ALIGN_CENTER, 0, 10);

    /* 試作0はU135エンコーダーのみで操作する方針のため、タッチでの
     * ドラッグ選択は無効化し、表示位置は常にnav側の選択インデックスに
     * 一本化する(タッチとエンコーダーの状態が食い違うのを防ぐ) */
    lv_obj_clear_flag(s_roller, LV_OBJ_FLAG_CLICKABLE);

    /* パラメータ調整画面(円ゲージ)用の2つ目のスクリーン。中身は
     * NAV_LEVEL_PARAMに最初に入ったときにbuild_param_gauges()が組み立てる
     * (デバッグ選択画面と同じ遅延生成方針)。 */
    s_param_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_param_screen, lv_color_hex(0x101010), 0);
    lv_obj_set_style_text_font(s_param_screen, &font_ja_menu_14, 0);

    lv_scr_load(s_screen); /* v9系では lv_screen_load() に読み替え可 */

    ui_screens_refresh();
}

void ui_screens_refresh(void)
{
    if (nav_get_state()->level == NAV_LEVEL_PARAM) {
        show_param_gauges();
        return;
    }

    char options_buf[1024];
    build_roller_options_string(options_buf, sizeof(options_buf));

    lv_roller_set_options(s_roller, options_buf, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(s_roller, nav_get_selected_index(), LV_ANIM_OFF);

    lv_label_set_text(s_title_label, level_title(nav_get_state()->level));
    lv_scr_load(s_screen);
}

void ui_screens_sync_selection(void)
{
    if (nav_get_state()->level == NAV_LEVEL_PARAM) {
        sync_param_gauges();
        return;
    }
    lv_roller_set_selected(s_roller, nav_get_selected_index(), LV_ANIM_ON);
}

/* ---- デバッグ用プリセット選択画面 ----
 * 通常のnav階層(menu_nav.c)とは無関係な、独立した2つ目の画面/ラベル/ローラー
 * を使う。通常起動では一度も使われないため、ここで初めて(遅延)生成する。 */

static lv_obj_t *s_debug_screen;
static lv_obj_t *s_debug_title_label;
static lv_obj_t *s_debug_roller;

static void ensure_debug_picker_created(void)
{
    if (s_debug_screen != NULL) {
        return;
    }

    s_debug_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_debug_screen, lv_color_hex(0x101010), 0);
    lv_obj_set_style_text_font(s_debug_screen, &font_ja_menu_14, 0);

    s_debug_title_label = lv_label_create(s_debug_screen);
    lv_obj_set_style_text_color(s_debug_title_label, lv_color_hex(0xffffff), 0);
    lv_obj_align(s_debug_title_label, LV_ALIGN_TOP_MID, 0, 10);
    lv_label_set_text(s_debug_title_label, "デバッグ: プリセット選択");

    s_debug_roller = lv_roller_create(s_debug_screen);
    lv_roller_set_visible_row_count(s_debug_roller, 4);
    lv_obj_set_width(s_debug_roller, 260);
    lv_obj_align(s_debug_roller, LV_ALIGN_CENTER, 0, 10);
    lv_obj_clear_flag(s_debug_roller, LV_OBJ_FLAG_CLICKABLE);
}

void ui_screens_show_debug_picker(const char *const *names, size_t count)
{
    ensure_debug_picker_created();

    char options_buf[1024];
    options_buf[0] = '\0';
    for (size_t i = 0; i < count; i++) {
        strncat(options_buf, names[i], sizeof(options_buf) - strlen(options_buf) - 1);
        if (i + 1 < count) {
            strncat(options_buf, "\n", sizeof(options_buf) - strlen(options_buf) - 1);
        }
    }

    lv_roller_set_options(s_debug_roller, options_buf, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(s_debug_roller, 0, LV_ANIM_OFF);

    lv_scr_load(s_debug_screen);
}

void ui_screens_debug_picker_set_selected(int index)
{
    lv_roller_set_selected(s_debug_roller, index, LV_ANIM_ON);
}
