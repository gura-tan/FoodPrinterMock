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
 *     時計回りにリングが伸びる。
 *     【要注意】lv_arc_set_bg_end_angle()は受け取った角度が360を超えると
 *     その場で-360する(`if (end > 360) end -= 360;`)ため、
 *     start+360(=630)をそのまま渡しても270に丸められてstartと一致し、
 *     スイープ幅が0になってリングが常に非表示になる(実際に一度これで
 *     ハマった)。正しくは終了角にstartより1度小さい値を渡すこと。
 *     lv_arc内部はend<startのときだけ+360する別のラップアラウンド処理を
 *     value_update()側で持っているため、これで359°(実質1周)のスイープ
 *     幅になる(1度分の隙間は半径44px程度の円では1px未満で視認できない)。
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
#define GAUGE_ANGLE_BG_END    (GAUGE_ANGLE_BG_START - 1) // 359°スイープ(実質1周、コメント参照)
#define GAUGE_COLOR_ACTIVE  0x66b3ff  // 操作中の円のアクセントカラー(暗背景での輝度コントラストを上げるため明るめの青に調整)
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

/* ---- 画面遷移(ワイプ)アニメーション ----
 * カテゴリ選択画面(大/小カテゴリ・メニュー選択の3階層で使い回すlv_roller)を
 * 離れるときだけ、rollerの白い枠を左→右に白で覆う→少し間を置く→隠れている
 * 間に中身を差し替える→左→右に白がはけて新しい画面が現れる、という演出を
 * 挟む(デザインスケッチ参照)。
 *
 * s_transition_maskはrollerと全く同じ位置/サイズ/角丸半径に毎回揃える
 * 透明なクリッピング用コンテナで、中の白い帯s_transition_bandは角丸無しの
 * ただの矩形。帯の「幅」だけを動かすことで、maskのclip_corner設定により
 * 本当の外側の角(=rollerの丸みと一致する場所)だけが丸くクリップされ、
 * 動いている境界線(帯のもう片方の辺)は常にまっすぐになる。帯自体に角丸を
 * 付けて幅を変えると、動いている境界まで丸まって見えてしまい
 * 「角丸の長方形なのに違和感がある」動きになるため、あえてこの二重構造に
 * している。
 * maskはlv_layer_top()配下に置く: MENU→PARAM遷移のように、覆い切った後の
 * 中身差し替えでアクティブ画面がs_screenからs_param_screenへ切り替わっても
 * (lv_scr_load()で別画面になっても)帯が消えずに最前面へ残り続けるように
 * するため。位置合わせはlv_obj_align_to()が双方の絶対座標で計算するので、
 * 親(画面)が違っても問題なく揃う。
 *
 * パラメータ調整画面(NAV_LEVEL_PARAM)にはrollerの白い枠自体が存在しない
 * ので演出を行わない。ただし「遷移先」ではなく「直前に表示していた画面
 * (=遷移元)」がPARAMかどうかで判定する(s_last_shown_level)。遷移先基準
 * だと、最後のSTART確定(DONE)でPARAM→MAJORへ戻る際に「遷移先はMAJOR=
 * カテゴリだから」と誤って再生されてしまい、rollerが実在しない状態からの
 * 遷移と噛み合わなくなるため。
 *
 * トランジション中はapp_main.c側がui_screens_transition_in_progress()を
 * 見て入力ポーリングを丸ごとスキップするため、「操作を中断された」感覚に
 * ならない程度の短さに抑えてある(値は実機で見ながら調整したもの)。 */
#define TRANSITION_COVER_MS     170
#define TRANSITION_HOLD_MS      100  // 覆い切ってからリビールを始めるまでの間(帯で隠れたまま静止)
#define TRANSITION_REVEAL_MS    200

static lv_obj_t    *s_transition_mask;  // rollerと同じ位置/サイズ/角丸のクリッピング用コンテナ(自身は透明)
static lv_obj_t    *s_transition_band;  // maskの子。角丸無しの単色矩形で、これの幅を動かす
static bool         s_transition_active;
static bool         s_transition_reverse; // true: 戻る操作(右→左)、false: 決定操作(左→右)
static nav_level_t  s_last_shown_level; // 直前にapply_refresh()で実際に表示した階層

static void apply_refresh(void)
{
    if (nav_get_state()->level == NAV_LEVEL_PARAM) {
        show_param_gauges();
    } else {
        char options_buf[1024];
        build_roller_options_string(options_buf, sizeof(options_buf));

        lv_roller_set_options(s_roller, options_buf, LV_ROLLER_MODE_NORMAL);
        lv_roller_set_selected(s_roller, nav_get_selected_index(), LV_ANIM_OFF);

        lv_label_set_text(s_title_label, level_title(nav_get_state()->level));
        lv_scr_load(s_screen);
    }
    s_last_shown_level = nav_get_state()->level;
}

static void transition_reveal_completed_cb(lv_anim_t *a)
{
    (void)a;
    lv_obj_add_flag(s_transition_mask, LV_OBJ_FLAG_HIDDEN);
    s_transition_active = false;
}

static void transition_cover_completed_cb(lv_anim_t *a)
{
    (void)a;
    apply_refresh(); // 帯で隠れている間に中身を差し替える

    if (nav_get_state()->level == NAV_LEVEL_PARAM) {
        /* パラメータ調整画面のゲージ列はroller(260px幅)よりも画面幅
         * いっぱいに並んでいる(GAUGE_SCREEN_W参照)。maskをrollerサイズの
         * ままrevealさせると、右端のSTART円がワイプの範囲外(=最初から
         * 覆われていない)になってしまい、中身差し替えの瞬間に先出しで
         * 見えてしまう(はがれていく境界線と重なって見える不具合の原因)。
         * covered状態(中身差し替え直後)はまだ帯で完全に隠れているうちに、
         * maskを画面全体のサイズへ広げ直してからrevealさせることで、
         * ゲージ列全体(STARTを含む)がワイプ演出の対象になるようにする。 */
        lv_obj_update_layout(s_param_screen);
        lv_obj_set_size(s_transition_mask, lv_obj_get_width(s_param_screen), lv_obj_get_height(s_param_screen));
        lv_obj_center(s_transition_mask);
        lv_obj_set_style_radius(s_transition_mask, 0, 0);
        lv_obj_set_height(s_transition_band, lv_obj_get_height(s_transition_mask));
    }

    /* 幅はcover完了時点でmaskの全幅と一致しているので、位置合わせの基準を
     * 切り替えても見た目はジャンプしない(どちらの基準でも「幅=maskの
     * 全幅」のときの座標は同じになるため)。以後は片方の端を固定したまま
     * 幅を縮めることで、もう片方の端(=境界線)だけが動く。
     * 通常(左→右): coverはLEFT_MID固定で伸ばした→revealはRIGHT_MID固定で
     * 縮め、境界線(左端)が左→右に動く。
     * 戻る(右→左): coverはRIGHT_MID固定で伸ばした→revealはLEFT_MID固定で
     * 縮め、境界線(右端)が右→左に動く(完全に鏡写しの動き)。
     * PARAM遷移の場合は上でmask/bandを広げ直しているので、幅もmask全幅へ
     * 揃え直す(そうしないと直前のrollerサイズの帯幅のまま位置だけ動いて
     * しまい、覆いが一瞬欠けて見える)。 */
    lv_obj_align(s_transition_band, s_transition_reverse ? LV_ALIGN_LEFT_MID : LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_width(s_transition_band, lv_obj_get_width(s_transition_mask));

    lv_anim_t reveal;
    lv_anim_init(&reveal);
    lv_anim_set_var(&reveal, s_transition_band);
    lv_anim_set_exec_cb(&reveal, (lv_anim_exec_xcb_t)lv_obj_set_width);
    lv_anim_set_values(&reveal, lv_obj_get_width(s_transition_mask), 0);
    lv_anim_set_duration(&reveal, TRANSITION_REVEAL_MS);
    lv_anim_set_delay(&reveal, TRANSITION_HOLD_MS);
    lv_anim_set_path_cb(&reveal, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&reveal, transition_reveal_completed_cb);
    lv_anim_start(&reveal);
}

static void start_transition(bool reverse)
{
    s_transition_active = true;
    s_transition_reverse = reverse;

    /* rollerの現在の位置/サイズ/角丸に毎回揃え直す(rollerは3階層とも
     * 同じ大きさで使い回しているので通常は変わらないが、念のため)。
     * update_layout()でレイアウトを確定させてからサイズを読む。 */
    lv_obj_update_layout(s_roller);
    lv_obj_set_size(s_transition_mask, lv_obj_get_width(s_roller), lv_obj_get_height(s_roller));
    lv_obj_align_to(s_transition_mask, s_roller, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(s_transition_mask, lv_obj_get_style_radius(s_roller, LV_PART_MAIN), 0);
    lv_obj_set_style_bg_color(s_transition_band, lv_obj_get_style_bg_color(s_roller, LV_PART_MAIN), 0);

    lv_obj_clear_flag(s_transition_mask, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(s_transition_band, lv_obj_get_height(s_transition_mask));
    /* 通常(左→右)はLEFT_MID固定で右端(=境界線)を右へ、戻る(右→左)は
     * RIGHT_MID固定で左端(=境界線)を左へ動かす。 */
    lv_obj_align(s_transition_band, reverse ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_width(s_transition_band, 0);

    lv_anim_t cover;
    lv_anim_init(&cover);
    lv_anim_set_var(&cover, s_transition_band);
    lv_anim_set_exec_cb(&cover, (lv_anim_exec_xcb_t)lv_obj_set_width);
    lv_anim_set_values(&cover, 0, lv_obj_get_width(s_transition_mask));
    lv_anim_set_duration(&cover, TRANSITION_COVER_MS);
    lv_anim_set_path_cb(&cover, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&cover, transition_cover_completed_cb);
    lv_anim_start(&cover);
}

bool ui_screens_transition_in_progress(void)
{
    return s_transition_active;
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

    /* ワイプ演出用のmask+band。lv_layer_top()配下なのでs_screen/
     * s_param_screenのどちらが表示中でも常に最前面に来る。位置/サイズ/
     * 角丸/色はrollerに合わせて毎回start_transition()側で揃え直すので、
     * ここでは箱を作るだけでよい。remove_style_all()でデフォルトテーマの
     * 余白/枠/角丸を消してある。 */
    s_transition_mask = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_transition_mask);
    lv_obj_set_style_bg_opa(s_transition_mask, LV_OPA_TRANSP, 0); // クリップ目的のみ、自身は透明
    lv_obj_set_style_clip_corner(s_transition_mask, true, 0);
    lv_obj_clear_flag(s_transition_mask, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_transition_mask, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_transition_mask, LV_OBJ_FLAG_HIDDEN);

    s_transition_band = lv_obj_create(s_transition_mask);
    lv_obj_remove_style_all(s_transition_band);
    lv_obj_set_style_bg_opa(s_transition_band, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_transition_band, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_transition_band, LV_OBJ_FLAG_SCROLLABLE);

    lv_scr_load(s_screen); /* v9系では lv_screen_load() に読み替え可 */

    apply_refresh(); // 起動直後の初回表示はワイプ演出無しで即時反映する
}

void ui_screens_refresh(bool is_back)
{
    /* ワイプ演出はrollerの白い枠を覆う/はがす動きなので、直前に表示して
     * いた画面(遷移元)がカテゴリ選択(roller)画面のときだけ行う。パラメータ
     * 調整画面(円ゲージ)から出るとき(START確定→大カテゴリへ戻るDONE操作を
     * 含む)はrollerが表示されていなかったので演出無しで即座に切り替える。
     * 「遷移先」で判定しないのは、その基準だとDONE操作(PARAM→MAJOR)を
     * 「遷移先はMAJOR=カテゴリだから」と誤って演出対象にしてしまうため。 */
    if (s_last_shown_level == NAV_LEVEL_PARAM) {
        apply_refresh();
        return;
    }
    start_transition(is_back); // 戻る操作は右→左、決定操作は左→右
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
