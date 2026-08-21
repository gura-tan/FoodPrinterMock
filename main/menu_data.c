#include "menu_data.h"

/* ---- 共有パラメータセット (デモ用: 量+塩味の2項目) ----
 * パラメータ調整画面(3つの円: 量→塩味→START)に合わせて2項目に絞っている。
 * 以前は「焼き加減」も含む3項目だったが、円のレイアウトをスケッチ
 * (main/../デザインスケッチ-パラメーター.png 相当)に合わせるため削除した。
 * 実データが決まったらここを差し替える。 */
static const parameter_def_t k_params_taste[] = {
    { .name = "量",   .caption = "量",       .unit = "g", .min_value = 100, .max_value = 500, .default_value = 200 },
    { .name = "塩味", .caption = "味の濃さ", .unit = "",  .min_value = 0,   .max_value = 10,  .default_value = 5   },
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

/* ================= 麺料理 ================= */
DEFINE_SINGLE_MENU(k_menu_ramen,     "ラーメン");
DEFINE_SINGLE_MENU(k_menu_udon,      "うどん");
DEFINE_SINGLE_MENU(k_menu_soba,      "そば");
DEFINE_SINGLE_MENU(k_menu_pasta,     "パスタ");
DEFINE_SINGLE_MENU(k_menu_yakisoba,  "焼きそば");
DEFINE_SINGLE_MENU(k_menu_pho,       "フォー");
DEFINE_SINGLE_MENU(k_menu_hiyachu,   "冷やし中華");
DEFINE_SINGLE_MENU(k_menu_tsukemen,  "つけ麺");

static const sub_category_def_t k_sub_noodle[] = {
    { .name = "ラーメン",     .menu_items = k_menu_ramen,    .menu_item_count = 1 },
    { .name = "うどん",       .menu_items = k_menu_udon,     .menu_item_count = 1 },
    { .name = "そば",         .menu_items = k_menu_soba,     .menu_item_count = 1 },
    { .name = "パスタ",       .menu_items = k_menu_pasta,    .menu_item_count = 1 },
    { .name = "焼きそば",     .menu_items = k_menu_yakisoba, .menu_item_count = 1 },
    { .name = "フォー",       .menu_items = k_menu_pho,      .menu_item_count = 1 },
    { .name = "冷やし中華",   .menu_items = k_menu_hiyachu,  .menu_item_count = 1 },
    { .name = "つけ麺",       .menu_items = k_menu_tsukemen, .menu_item_count = 1 },
};

/* ================= ご飯料理 ================= */
DEFINE_SINGLE_MENU(k_menu_takikomi,  "炊き込みご飯");
DEFINE_SINGLE_MENU(k_menu_curryrice, "カレーライス");
DEFINE_SINGLE_MENU(k_menu_chahan,    "チャーハン");
DEFINE_SINGLE_MENU(k_menu_omurice,   "オムライス");
DEFINE_SINGLE_MENU(k_menu_donburi,   "丼もの");
DEFINE_SINGLE_MENU(k_menu_risotto,   "リゾット");
DEFINE_SINGLE_MENU(k_menu_paella,    "パエリア");
DEFINE_SINGLE_MENU(k_menu_onigiri,   "おにぎり");

static const sub_category_def_t k_sub_rice[] = {
    { .name = "炊き込みご飯", .menu_items = k_menu_takikomi,  .menu_item_count = 1 },
    { .name = "カレーライス", .menu_items = k_menu_curryrice, .menu_item_count = 1 },
    { .name = "チャーハン",   .menu_items = k_menu_chahan,    .menu_item_count = 1 },
    { .name = "オムライス",   .menu_items = k_menu_omurice,   .menu_item_count = 1 },
    { .name = "丼もの",       .menu_items = k_menu_donburi,   .menu_item_count = 1 },
    { .name = "リゾット",     .menu_items = k_menu_risotto,   .menu_item_count = 1 },
    { .name = "パエリア",     .menu_items = k_menu_paella,    .menu_item_count = 1 },
    { .name = "おにぎり",     .menu_items = k_menu_onigiri,   .menu_item_count = 1 },
};

/* ================= パン・粉もの ================= */
DEFINE_SINGLE_MENU(k_menu_shokupan,  "食パン");
DEFINE_SINGLE_MENU(k_menu_pizza,     "ピザ");
DEFINE_SINGLE_MENU(k_menu_okonomi,   "お好み焼き");
DEFINE_SINGLE_MENU(k_menu_takoyaki,  "たこ焼き");
DEFINE_SINGLE_MENU(k_menu_crepe,     "クレープ");
DEFINE_SINGLE_MENU(k_menu_waffle,    "ワッフル");
DEFINE_SINGLE_MENU(k_menu_sandwich,  "サンドイッチ");
DEFINE_SINGLE_MENU(k_menu_hotcake,   "ホットケーキ");

static const sub_category_def_t k_sub_flour[] = {
    { .name = "食パン",       .menu_items = k_menu_shokupan, .menu_item_count = 1 },
    { .name = "ピザ",         .menu_items = k_menu_pizza,    .menu_item_count = 1 },
    { .name = "お好み焼き",   .menu_items = k_menu_okonomi,  .menu_item_count = 1 },
    { .name = "たこ焼き",     .menu_items = k_menu_takoyaki, .menu_item_count = 1 },
    { .name = "クレープ",     .menu_items = k_menu_crepe,    .menu_item_count = 1 },
    { .name = "ワッフル",     .menu_items = k_menu_waffle,   .menu_item_count = 1 },
    { .name = "サンドイッチ", .menu_items = k_menu_sandwich, .menu_item_count = 1 },
    { .name = "ホットケーキ", .menu_items = k_menu_hotcake,  .menu_item_count = 1 },
};

/* ================= 鍋・煮込み ================= */
DEFINE_SINGLE_MENU(k_menu_yosenabe,  "寄せ鍋");
DEFINE_SINGLE_MENU(k_menu_sukiyaki,  "すき焼き");
DEFINE_SINGLE_MENU(k_menu_shabu,     "しゃぶしゃぶ");
DEFINE_SINGLE_MENU(k_menu_oden,      "おでん");
DEFINE_SINGLE_MENU(k_menu_curry,     "カレー");
DEFINE_SINGLE_MENU(k_menu_stew,      "シチュー");
DEFINE_SINGLE_MENU(k_menu_motsunabe, "もつ鍋");
DEFINE_SINGLE_MENU(k_menu_potofu,    "ポトフ");

static const sub_category_def_t k_sub_hotpot[] = {
    { .name = "寄せ鍋",       .menu_items = k_menu_yosenabe,  .menu_item_count = 1 },
    { .name = "すき焼き",     .menu_items = k_menu_sukiyaki,  .menu_item_count = 1 },
    { .name = "しゃぶしゃぶ", .menu_items = k_menu_shabu,     .menu_item_count = 1 },
    { .name = "おでん",       .menu_items = k_menu_oden,      .menu_item_count = 1 },
    { .name = "カレー",       .menu_items = k_menu_curry,     .menu_item_count = 1 },
    { .name = "シチュー",     .menu_items = k_menu_stew,      .menu_item_count = 1 },
    { .name = "もつ鍋",       .menu_items = k_menu_motsunabe, .menu_item_count = 1 },
    { .name = "ポトフ",       .menu_items = k_menu_potofu,    .menu_item_count = 1 },
};

/* ================= サラダ・野菜料理 ================= */
DEFINE_SINGLE_MENU(k_menu_greensalad, "グリーンサラダ");
DEFINE_SINGLE_MENU(k_menu_potatosalad,"ポテトサラダ");
DEFINE_SINGLE_MENU(k_menu_onyasai,    "温野菜");
DEFINE_SINGLE_MENU(k_menu_ratatouille,"ラタトゥイユ");
DEFINE_SINGLE_MENU(k_menu_kinpira,    "きんぴら");
DEFINE_SINGLE_MENU(k_menu_asazuke,    "浅漬け");
DEFINE_SINGLE_MENU(k_menu_coleslaw,   "コールスロー");
DEFINE_SINGLE_MENU(k_menu_vegsoup,    "野菜スープ");

static const sub_category_def_t k_sub_vegetable[] = {
    { .name = "グリーンサラダ", .menu_items = k_menu_greensalad,  .menu_item_count = 1 },
    { .name = "ポテトサラダ",   .menu_items = k_menu_potatosalad, .menu_item_count = 1 },
    { .name = "温野菜",         .menu_items = k_menu_onyasai,     .menu_item_count = 1 },
    { .name = "ラタトゥイユ",   .menu_items = k_menu_ratatouille, .menu_item_count = 1 },
    { .name = "きんぴら",       .menu_items = k_menu_kinpira,     .menu_item_count = 1 },
    { .name = "浅漬け",         .menu_items = k_menu_asazuke,     .menu_item_count = 1 },
    { .name = "コールスロー",   .menu_items = k_menu_coleslaw,    .menu_item_count = 1 },
    { .name = "野菜スープ",     .menu_items = k_menu_vegsoup,     .menu_item_count = 1 },
};

/* ================= デザート・軽食 ================= */
DEFINE_SINGLE_MENU(k_menu_pudding,   "プリン");
DEFINE_SINGLE_MENU(k_menu_icecream,  "アイスクリーム");
DEFINE_SINGLE_MENU(k_menu_cake,      "ケーキ");
DEFINE_SINGLE_MENU(k_menu_donut,     "ドーナツ");
DEFINE_SINGLE_MENU(k_menu_cookie,    "クッキー");
DEFINE_SINGLE_MENU(k_menu_fruitpunch,"フルーツポンチ");
DEFINE_SINGLE_MENU(k_menu_parfait,   "パフェ");
DEFINE_SINGLE_MENU(k_menu_jelly,     "ゼリー");

static const sub_category_def_t k_sub_dessert[] = {
    { .name = "プリン",         .menu_items = k_menu_pudding,    .menu_item_count = 1 },
    { .name = "アイスクリーム", .menu_items = k_menu_icecream,   .menu_item_count = 1 },
    { .name = "ケーキ",         .menu_items = k_menu_cake,       .menu_item_count = 1 },
    { .name = "ドーナツ",       .menu_items = k_menu_donut,      .menu_item_count = 1 },
    { .name = "クッキー",       .menu_items = k_menu_cookie,     .menu_item_count = 1 },
    { .name = "フルーツポンチ", .menu_items = k_menu_fruitpunch, .menu_item_count = 1 },
    { .name = "パフェ",         .menu_items = k_menu_parfait,    .menu_item_count = 1 },
    { .name = "ゼリー",         .menu_items = k_menu_jelly,      .menu_item_count = 1 },
};

/* ================= 大カテゴリ =================
 * Notionには肉料理/魚介料理以外の小カテゴリがまだ具体的に列挙されていないため、
 * 残り6カテゴリはデモ用の仮データ(一般的な料理名)で埋めている。
 * 実データが決まり次第、各配列の中身を差し替えるだけでよい。
 */
const major_category_def_t g_major_categories[] = {
    { .name = "肉料理",           .sub_categories = k_sub_meat,      .sub_category_count = 10 },
    { .name = "魚介料理",         .sub_categories = k_sub_seafood,   .sub_category_count = 10 },
    { .name = "麺料理",           .sub_categories = k_sub_noodle,    .sub_category_count = 8 },
    { .name = "ご飯料理",         .sub_categories = k_sub_rice,      .sub_category_count = 8 },
    { .name = "パン・粉もの",     .sub_categories = k_sub_flour,     .sub_category_count = 8 },
    { .name = "鍋・煮込み",       .sub_categories = k_sub_hotpot,    .sub_category_count = 8 },
    { .name = "サラダ・野菜料理", .sub_categories = k_sub_vegetable, .sub_category_count = 8 },
    { .name = "デザート・軽食",   .sub_categories = k_sub_dessert,   .sub_category_count = 8 },
};
const size_t g_major_category_count = sizeof(g_major_categories) / sizeof(g_major_categories[0]);
