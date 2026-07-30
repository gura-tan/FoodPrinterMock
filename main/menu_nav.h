#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "menu_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 画面階層のナビゲーション状態管理。
 * 「現在の階層 + 選択パス」を1つの状態(スタック相当)として保持し、
 * 決定操作で1階層深く進み、キャンセル操作で1階層戻る。
 *
 * パラメータ調整画面(NAV_LEVEL_PARAM)も、選択画面と全く同じ
 * 「文字列の配列から1つ選ぶ」というインターフェースに統一している
 * (min〜maxの値を文字列化した選択肢をローラーに並べるだけ)。
 * これにより画面側は4階層すべてを同じ1つのテンプレートで描画できる。
 */

typedef enum {
    NAV_LEVEL_MAJOR = 0,   // 大カテゴリ選択
    NAV_LEVEL_SUB,         // 小カテゴリ選択
    NAV_LEVEL_MENU,        // メニュー選択
    NAV_LEVEL_PARAM,       // パラメータ調整 (parameter_index で1画面/1パラメータ)
} nav_level_t;

typedef struct {
    nav_level_t level;
    int major_index;
    int sub_index;
    int menu_index;
    int parameter_index;   // NAV_LEVEL_PARAMのときのみ意味を持つ
} nav_state_t;

void nav_init(void);
const nav_state_t *nav_get_state(void);

/* 現在の階層で選べる項目名(または数値の文字列)の配列を取得する。
 * out_countに件数を書き込み、内部の静的バッファを指すポインタ配列を返す。
 * 戻り値は次にnav_*系関数を呼ぶまで有効。 */
const char *const *nav_get_current_options(size_t *out_count);

/* エンコーダーのdeltaぶんだけ現在の選択インデックスを移動する(範囲外はクランプ) */
void nav_move_selection(int delta);
int  nav_get_selected_index(void);

/* 「決定」操作: 現在の選択で1階層深く進む。
 * 戻り値: これでパラメータをすべて確定し、一連の操作フローが完了したらtrue */
bool nav_confirm(void);

/* 「キャンセル/戻る」操作: 1階層戻る。
 * 大カテゴリ画面(最上位)で呼ばれた場合は何もしない。
 * 専用の取り消しボタンがまだ無いため、当面はエンコーダーボタンの長押しから
 * この関数を呼ぶ運用を想定している(app_main.c参照)。 */
void nav_back(void);

/* NAV_LEVEL_PARAMで確定済みのパラメータ値を取得する(index=parameter_index) */
int32_t nav_get_confirmed_param_value(int index);

#ifdef __cplusplus
}
#endif
