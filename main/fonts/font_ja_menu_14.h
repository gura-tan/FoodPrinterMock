#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * アプリのメニュー画面(ui_screens.c)で実際に表示する文字だけを収録した
 * 独自フォント(Noto Sans JP由来、14px/4bpp)。
 *
 * 従来はLVGLのKconfigが提供する CONFIG_LV_FONT_SOURCE_HAN_SANS_SC_14_CJK を
 * デフォルトフォントとして使っていたが、これは簡体字中国語向けの字形セット
 * のため、日本語の「揚」「麺」等の字形が含まれておらず豆腐(□)表示になって
 * いた。LVGL/menuconfigには日本語向けのビルトインフォントが無いため、
 * 必要な文字だけをNoto Sans JPから抜き出して自前でフォントを生成した
 * (main/fonts/chars.txt に収録文字の一覧、font_ja_menu_14.c 冒頭のコメントに
 * 生成コマンドが残っているので、文字を追加するときはchars.txtを更新して
 * 同じコマンドで再生成すること)。
 */
LV_FONT_DECLARE(font_ja_menu_14);

#ifdef __cplusplus
}
#endif
