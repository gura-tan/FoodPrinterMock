#pragma once
#include "menu_nav.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 大カテゴリ/小カテゴリ/メニュー選択/パラメータ調整の4階層すべてを
 * 「タイトルラベル + lv_roller」1組だけで使い回す画面。
 * これらの関数はLVGLのAPIを直接呼ぶため、呼び出し側で
 * bsp_display_lock()/unlock() による排他を取ってから呼ぶこと。 */

void ui_screens_init(void);

/* nav側の階層/選択パスが変わった(決定/戻る操作をした)直後に呼ぶ:
 * タイトルとローラーの選択肢を丸ごと再構築する */
void ui_screens_refresh(void);

/* エンコーダー回転でnav_move_selection()を呼んだ直後に呼ぶ:
 * ローラーの表示位置だけをアニメーション付きで更新する(選択肢の再構築はしない) */
void ui_screens_sync_selection(void);

#ifdef __cplusplus
}
#endif
