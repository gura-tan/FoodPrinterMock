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
 * 大カテゴリ/小カテゴリ/メニュー選択は「文字列の配列から1つ選ぶ」
 * インターフェースに統一しており、画面側は同じテンプレート(ui_screens.c)で
 * 描画できる(nav_get_current_options()参照)。
 *
 * パラメータ調整(NAV_LEVEL_PARAM)だけは専用の円ゲージ画面を持つため別扱い。
 * parameter_indexは 0..parameter_count-1 が各パラメータ(ダイヤルで値を調整)、
 * parameter_count(=最後の次のインデックス)は「START」を表す仮想ステップで、
 * 値を持たず決定操作だけを受け付ける(nav_confirm()参照)。
 *
 * NAV_LEVEL_COOKINGはSTART確定後に入る調理中(カウントダウン)画面。他の階層と
 * 違い「選択肢から1つ選ぶ」モデルではなく実時間で進む状態なので、
 * nav_get_current_options()等は使わずnav_cooking_*()系の専用APIで扱う。 */

typedef enum {
    NAV_LEVEL_MAJOR = 0,   // 大カテゴリ選択
    NAV_LEVEL_SUB,         // 小カテゴリ選択
    NAV_LEVEL_MENU,        // メニュー選択
    NAV_LEVEL_PARAM,       // パラメータ調整 (parameter_index で1画面/1パラメータ、末尾はSTART)
    NAV_LEVEL_COOKING,     // 調理中(カウントダウン)。STARTの確定で入る
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

/* エンコーダーのdeltaぶんだけ現在の選択インデックスを移動する(範囲外はクランプ)。
 * 戻り値: 実際に選択インデックスが変化したらtrue。既にリストの端にいて
 * その方向へさらに回した場合(=クランプされて変化しなかった場合)はfalseを
 * 返すので、呼び出し側はこれを見てdeny音を鳴らし分けられる。 */
bool nav_move_selection(int delta);
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

/* 現在選択中のメニュー項目(パラメータ定義配列の元)を返す。NAV_LEVEL_PARAM画面が
 * 各パラメータの名前/単位/min/maxを読むために使う。他階層で呼んでも安全だが
 * 意味を持つのはNAV_LEVEL_PARAMのときだけ。 */
const menu_item_def_t *nav_get_current_menu_item(void);

/* NAV_LEVEL_PARAMで、まだ確定していない「今ダイヤルで選んでいる値」込みの
 * 表示用の値を返す: index==parameter_index(操作中)ならダイヤルの現在位置から
 * 計算し、それ以外(確定済み/未到達)はnav_get_confirmed_param_value()と同じ値
 * (未到達なら0)を返す。円ゲージ画面がダイヤル操作に追従して値を表示するために使う。 */
int32_t nav_get_param_live_value(int index);

/* ---- 調理中画面(カウントダウン) ----
 * 実時間で進む状態はここ(モデル層)で保持し、ui_screens.cは毎フレーム
 * 読み出して円/ラベルを更新するだけにする(パラメータ調整画面のダイヤル
 * 追従表示と同じ設計)。nav_confirm()がSTART確定時に内部でnav_cooking_start()
 * を呼ぶため、呼び出し側から直接呼ぶことは通常ない。 */

/* total_seconds分のカウントダウンを開始する。 */
void nav_cooking_start(int32_t total_seconds);

/* 実時間の経過ぶんだけ残り時間を減らす。app_main.c/sim_main.cのメインループから
 * (NAV_LEVEL_COOKINGの間は)毎回呼ぶこと。
 * 戻り値: この呼び出しでちょうど0に達した(=完了に遷移した)瞬間だけtrue
 * (readyサウンドの再生トリガー用)。 */
bool nav_cooking_tick(void);

/* ダイヤル入力で残り時間を早送り/巻き戻しする(実演用)。delta>0で減らす方向
 * (デモで待ち時間を早送りする用途を優先している)、delta<0で増やす方向。
 * 完了後(残り0)は常に無効化されfalseを返す。
 * 戻り値: 実際に値が変化したらtrue(MOVE/DENY音の判定に使う)。 */
bool nav_cooking_adjust(int32_t delta);

int32_t nav_cooking_remaining_seconds(void);
int32_t nav_cooking_total_seconds(void);
bool    nav_cooking_is_complete(void);

#ifdef __cplusplus
}
#endif
