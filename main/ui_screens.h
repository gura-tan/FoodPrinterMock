#pragma once
#include "menu_nav.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 大カテゴリ/小カテゴリ/メニュー選択の3階層は「タイトルラベル + lv_roller」
 * 1組だけで使い回す画面。パラメータ調整(NAV_LEVEL_PARAM)だけは横並びの円
 * ゲージを使う専用画面(内部で別のlv_obj画面として持ち、切り替えて表示する)。
 * これらの関数はLVGLのAPIを直接呼ぶため、呼び出し側で
 * bsp_display_lock()/unlock() による排他を取ってから呼ぶこと。 */

void ui_screens_init(void);

/* nav側の階層/選択パスが変わった(決定/戻る操作をした)直後に呼ぶ:
 * タイトルとローラーの選択肢を丸ごと再構築する */
void ui_screens_refresh(void);

/* エンコーダー回転でnav_move_selection()を呼んだ直後に呼ぶ:
 * ローラーの表示位置だけをアニメーション付きで更新する(選択肢の再構築はしない) */
void ui_screens_sync_selection(void);

/* ui_screens_refresh()が起こす画面遷移(ワイプ)アニメーションの最中はtrue。
 * 呼び出し側(app_main.c)はこの間、入力ポーリングを丸ごとスキップして
 * ボタン/エンコーダー操作を受け付けないようにする。 */
bool ui_screens_transition_in_progress(void);

/* デバッグ用プリセット選択画面(起動時ボタン長押しで入る、通常のnav階層とは
 * 無関係な独立画面)を表示する。namesはcount個の文字列配列で、この呼び出し中に
 * ローラーへコピーされるため、呼び出し後にnames自体は解放してよい。
 * 初回呼び出し時に画面/ラベル/ローラーを生成する(通常起動では一度も使われない
 * ため、ui_screens_init()では事前生成しない)。 */
void ui_screens_show_debug_picker(const char *const *names, size_t count);

/* デバッグ選択画面のローラー選択位置だけを更新する(ui_screens_sync_selection()の
 * デバッグ画面版)。 */
void ui_screens_debug_picker_set_selected(int index);

#ifdef __cplusplus
}
#endif
