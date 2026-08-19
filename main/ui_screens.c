#include "ui_screens.h"
#include "menu_data.h"
#include "lvgl.h"
#include "fonts/font_ja_menu_14.h"
#include <string.h>

/*
 * SquareLine Studioは使わず、LVGLのC APIを直接呼んで組んでいる。
 *
 * 注意(要確認): lv_scr_load() / lv_screen_load() や一部のスタイルAPIは
 * LVGLのバージョン(v8系 / v9系)で名前が異なることがある。BSPが依存している
 * LVGLのバージョンを managed_components/lvgl/library.json 等で確認し、
 * 差異があれば適宜読み替えてほしい(本コードはv8.3〜v9.2あたりの
 * 共通APIを想定して書いている)。
 */

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

    lv_scr_load(s_screen); /* v9系では lv_screen_load() に読み替え可 */

    ui_screens_refresh();
}

void ui_screens_refresh(void)
{
    char options_buf[1024];
    build_roller_options_string(options_buf, sizeof(options_buf));

    lv_roller_set_options(s_roller, options_buf, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(s_roller, nav_get_selected_index(), LV_ANIM_OFF);

    lv_label_set_text(s_title_label, level_title(nav_get_state()->level));
}

void ui_screens_sync_selection(void)
{
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
