# FetchContentのPATCH_COMMANDから `cmake -DSMALL_MAX=<px> -P` で呼ばれる。
# LVGLのデフォルトテーマは「長辺 <= 320px」を小画面とみなして余白/角丸を決める。
# simをUI_SCALE倍の解像度(例: 1280x960)で動かすと「大画面」扱いになって余白が
# 変わり、実機と見た目がずれるため、小画面の判定しきい値をUI_SCALE倍に引き上げる。
# (ソースの該当行が見つからなければ何もしない。何度実行しても同じ結果になる)
if(NOT DEFINED SMALL_MAX)
    message(FATAL_ERROR "SMALL_MAX is not set")
endif()
set(_f "src/themes/default/lv_theme_default.c")
file(READ "${_f}" _src)
string(REGEX REPLACE "greater_res <= [0-9]+\\)" "greater_res <= ${SMALL_MAX})" _new "${_src}")
if(NOT _new STREQUAL _src)
    file(WRITE "${_f}" "${_new}")
    message(STATUS "patched ${_f}: small display threshold = ${SMALL_MAX}px")
endif()
