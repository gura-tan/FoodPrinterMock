#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 試作0 メニューデータモデル
 * ------------------------------------------------------------
 * Notion「表現 フードプリンター」→「設定 / コンセプト」→「操作フロー」→「カテゴリ」を
 * ソースにした階層データ。
 *
 * 階層: 大カテゴリ → 小カテゴリ → メニュー → パラメータ[]
 *
 * 画面(UI)側は本データを「配列を渡すだけで再利用できる1つの選択画面テンプレート」で
 * 描画するため、ここに項目を追加するだけで画面を増やさずに拡張できる。
 *
 * 【現状の注意点】
 * Notion側には現時点で「肉料理」「魚介料理」の小カテゴリしか具体的に列挙されておらず、
 * 残り6つの大カテゴリ(麺料理/ご飯料理/パン・粉もの/鍋・煮込み/サラダ・野菜料理/
 * デザート・軽食)は小カテゴリが未入力("…etc")。フードプリンターのデモという性質上
 * カテゴリの中身自体の正確さは重要ではないため、本データではこの6つも一般的な
 * 料理名でデモ用に仮埋めしている。実データが決まり次第 menu_data.c の該当配列を
 * 差し替えればよい。
 * また「メニュー」階層(小カテゴリのさらに先)もNotionに具体データが無いため、
 * デモ用に小カテゴリと同名のメニュー項目を1つだけ仮で持たせ、味調整用の
 * パラメータも仮の値(量/塩味)を設定している。実データが決まり次第
 * menu_data.c を編集するだけでよい。
 */

typedef struct {
    const char *name;          // パラメータ名 (例: "塩味")
    const char *unit;          // 単位。無ければ空文字列 ""
    int32_t     min_value;
    int32_t     max_value;
    int32_t     default_value; // 現状は未使用(将来、初期選択位置に使う想定)
} parameter_def_t;

typedef struct {
    const char             *name;              // メニュー名
    const parameter_def_t  *parameters;
    size_t                  parameter_count;
} menu_item_def_t;

typedef struct {
    const char             *name;              // 小カテゴリ名
    const menu_item_def_t  *menu_items;
    size_t                  menu_item_count;
} sub_category_def_t;

typedef struct {
    const char                 *name;          // 大カテゴリ名
    const sub_category_def_t   *sub_categories;
    size_t                      sub_category_count;
} major_category_def_t;

extern const major_category_def_t g_major_categories[];
extern const size_t               g_major_category_count;

#ifdef __cplusplus
}
#endif
