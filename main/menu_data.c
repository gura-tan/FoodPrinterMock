#include "menu_data.h"

/* ---- 共有パラメータセット (デモ用: 量+塩味の2項目) ----
 * パラメータ調整画面(3つの円: 量→塩味→START)に合わせて2項目に絞っている。
 * 以前は「焼き加減」も含む3項目だったが、円のレイアウトをスケッチ
 * (main/../デザインスケッチ-パラメーター.png 相当)に合わせるため削除した。
 * 実データが決まったらここを差し替える。 */
static const parameter_def_t k_params_taste[] = {
    { .name = "量",   .unit = "g", .min_value = 100, .max_value = 500, .default_value = 200 },
    { .name = "塩味", .unit = "",  .min_value = 0,   .max_value = 10,  .default_value = 5   },
};
#define TASTE_PARAM_COUNT (sizeof(k_params_taste) / sizeof(k_params_taste[0]))

/* 小カテゴリと同名のメニュー項目を1件だけ仮で持たせるためのマクロ */
#define DEFINE_SINGLE_MENU(varname, label) \
    static const menu_item_def_t varname[] = { \
        { .name = label, .parameters = k_params_taste, .parameter_count = TASTE_PARAM_COUNT }, \
    }

/* ================= 肉料理 ================= */
DEFINE_SINGLE_MENU(k_menu_steak,    "ステーキ");
DEFINE_SINGLE_MENU(k_menu_hamburg,  "ハンバーグ");
DEFINE_SINGLE_MENU(k_menu_yakiniku, "焼肉");
DEFINE_SINGLE_MENU(k_menu_karaage,  "唐揚げ");
DEFINE_SINGLE_MENU(k_menu_tonkatsu, "とんかつ");
DEFINE_SINGLE_MENU(k_menu_yakitori, "焼き鳥");
DEFINE_SINGLE_MENU(k_menu_sausage,  "ソーセージ");
DEFINE_SINGLE_MENU(k_menu_roast,    "ロースト");
DEFINE_SINGLE_MENU(k_menu_kakuni,   "角煮");
DEFINE_SINGLE_MENU(k_menu_sparerib, "スペアリブ");

static const sub_category_def_t k_sub_meat[] = {
    { .name = "ステーキ",   .menu_items = k_menu_steak,    .menu_item_count = 1 },
    { .name = "ハンバーグ", .menu_items = k_menu_hamburg,  .menu_item_count = 1 },
    { .name = "焼肉",       .menu_items = k_menu_yakiniku, .menu_item_count = 1 },
    { .name = "唐揚げ",     .menu_items = k_menu_karaage,  .menu_item_count = 1 },
    { .name = "とんかつ",   .menu_items = k_menu_tonkatsu, .menu_item_count = 1 },
    { .name = "焼き鳥",     .menu_items = k_menu_yakitori, .menu_item_count = 1 },
    { .name = "ソーセージ", .menu_items = k_menu_sausage,  .menu_item_count = 1 },
    { .name = "ロースト",   .menu_items = k_menu_roast,    .menu_item_count = 1 },
    { .name = "角煮",       .menu_items = k_menu_kakuni,   .menu_item_count = 1 },
    { .name = "スペアリブ", .menu_items = k_menu_sparerib, .menu_item_count = 1 },
};

/* ================= 魚介料理 ================= */
DEFINE_SINGLE_MENU(k_menu_sashimi,    "刺身");
DEFINE_SINGLE_MENU(k_menu_sushi,      "寿司");
DEFINE_SINGLE_MENU(k_menu_yakizakana, "焼き魚");
DEFINE_SINGLE_MENU(k_menu_nizakana,   "煮魚");
DEFINE_SINGLE_MENU(k_menu_fry,        "フライ");
DEFINE_SINGLE_MENU(k_menu_tempura,    "天ぷら");
DEFINE_SINGLE_MENU(k_menu_carpaccio,  "カルパッチョ");
DEFINE_SINGLE_MENU(k_menu_meuniere,   "ムニエル");
DEFINE_SINGLE_MENU(k_menu_aquapazza,  "アクアパッツァ");
DEFINE_SINGLE_MENU(k_menu_seafoodfry, "シーフード炒め");

static const sub_category_def_t k_sub_seafood[] = {
    { .name = "刺身",           .menu_items = k_menu_sashimi,    .menu_item_count = 1 },
    { .name = "寿司",           .menu_items = k_menu_sushi,      .menu_item_count = 1 },
    { .name = "焼き魚",         .menu_items = k_menu_yakizakana, .menu_item_count = 1 },
    { .name = "煮魚",           .menu_items = k_menu_nizakana,   .menu_item_count = 1 },
    { .name = "フライ",         .menu_items = k_menu_fry,        .menu_item_count = 1 },
    { .name = "天ぷら",         .menu_items = k_menu_tempura,    .menu_item_count = 1 },
    { .name = "カルパッチョ",   .menu_items = k_menu_carpaccio,  .menu_item_count = 1 },
    { .name = "ムニエル",       .menu_items = k_menu_meuniere,   .menu_item_count = 1 },
    { .name = "アクアパッツァ", .menu_items = k_menu_aquapazza,  .menu_item_count = 1 },
    { .name = "シーフード炒め", .menu_items = k_menu_seafoodfry, .menu_item_count = 1 },
};

/* ================= 大カテゴリ =================
 * 残り6カテゴリはNotionに小カテゴリがまだ無いため空(0件)のプレースホルダー。
 * データが決まったら k_sub_meat / k_sub_seafood と同じ形式で配列を作り、
 * ここの sub_categories / sub_category_count を差し替えるだけでよい。
 */
const major_category_def_t g_major_categories[] = {
    { .name = "肉料理",           .sub_categories = k_sub_meat,    .sub_category_count = 10 },
    { .name = "魚介料理",         .sub_categories = k_sub_seafood, .sub_category_count = 10 },
    { .name = "麺料理",           .sub_categories = NULL, .sub_category_count = 0 }, /* TODO */
    { .name = "ご飯料理",         .sub_categories = NULL, .sub_category_count = 0 }, /* TODO */
    { .name = "パン・粉もの",     .sub_categories = NULL, .sub_category_count = 0 }, /* TODO */
    { .name = "鍋・煮込み",       .sub_categories = NULL, .sub_category_count = 0 }, /* TODO */
    { .name = "サラダ・野菜料理", .sub_categories = NULL, .sub_category_count = 0 }, /* TODO */
    { .name = "デザート・軽食",   .sub_categories = NULL, .sub_category_count = 0 }, /* TODO */
};
const size_t g_major_category_count = sizeof(g_major_categories) / sizeof(g_major_categories[0]);
