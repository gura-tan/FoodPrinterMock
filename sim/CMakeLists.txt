# PCブラウザ(Emscripten/WASM)向けのUIシミュレータ。
# main/ 以下のうちハードウェア非依存な画面ロジック(menu_data/menu_nav/
# ui_screens + フォント/アイコン)だけを、実機のロータリーエンコーダーの
# 代わりにキーボード入力で操作するsim_main.cと組み合わせてビルドする。
#
# ビルド方法(Emscripten SDKを事前にactivateしておくこと):
#   source ~/emsdk/emsdk_env.sh
#   emcmake cmake -S sim -B sim/build
#   cmake --build sim/build
#   python3 -m http.server -d sim/build 8000   # ブラウザで http://localhost:8000/index.html を開く
#
# 【高解像度ビルド】SIM_UI_SCALE(既定4)倍の解像度(320x240 -> 1280x960)で描画する。
# 発表資料用に画面をPNG保存したときに、文字や線が荒くならないようにするため。
# 実機ビルド(main/)には影響しない(UI_SCALE未定義=1倍のまま)。
# 等倍に戻したいときは `-DSIM_UI_SCALE=1` を付けてcmakeを実行する(simのフォント/
# アイコンの差し替えも自動で外れる)。
cmake_minimum_required(VERSION 3.16)
project(food_printer_sim C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(SIM_UI_SCALE 4 CACHE STRING "Render scale of the simulator UI (1 or 4)")
if(NOT (SIM_UI_SCALE EQUAL 1 OR SIM_UI_SCALE EQUAL 4))
    message(FATAL_ERROR "SIM_UI_SCALE must be 1 or 4 (hi-res font/icon assets exist for 4 only)")
endif()
add_compile_definitions(UI_SCALE=${SIM_UI_SCALE})

# lv_conf.hはそのまま使い、スクリーンショットに必要な LV_USE_SNAPSHOT だけを
# 1にした複製をビルドディレクトリに作ってLVGLに渡す(lv_conf.h本体は編集不要)。
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" _lv_conf_src)
string(REGEX REPLACE "#define LV_USE_SNAPSHOT[ \t]+0" "#define LV_USE_SNAPSHOT 1" _lv_conf_gen "${_lv_conf_src}")
file(WRITE "${CMAKE_BINARY_DIR}/lv_conf_gen/lv_conf.h" "${_lv_conf_gen}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h")

set(LV_BUILD_CONF_PATH "${CMAKE_BINARY_DIR}/lv_conf_gen/lv_conf.h" CACHE PATH "" FORCE)
option(CONFIG_LV_BUILD_DEMOS "" OFF)
option(CONFIG_LV_BUILD_EXAMPLES "" OFF)
option(CONFIG_LV_USE_THORVG_INTERNAL "" OFF)

if(EMSCRIPTEN)
    # lvglライブラリ自身がsrc/drivers/sdl/*.cで<SDL2/SDL.h>をincludeするため、
    # lvglターゲットのコンパイルにも-sUSE_SDL=2が要る(sim側だけでは効かない)。
    add_compile_options("-sUSE_SDL=2")
endif()

# managed_components/lvgl__lvglはESP-IDFのコンポーネントマネージャがidf.py build時に
# 取得するもので.gitignore対象のため、cloneしただけのリポジトリ(Vercel等)には
# 存在しない。simはそれに依存させず、main/側と同じバージョン(9.5.0。
# main/ui_screens.c等の実装もこのバージョンのAPIを前提にしている)のLVGLを
# GitHubから直接取得してビルドする。
# 取得したLVGLには patch_lvgl_theme.cmake を当てる(高解像度でもテーマの余白が
# 実機と同じ「小画面向け」のままになるようにするため)。
include(FetchContent)
math(EXPR SIM_THEME_SMALL_MAX "320 * ${SIM_UI_SCALE}")
FetchContent_Declare(
    lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v9.5.0
    GIT_SHALLOW TRUE
    PATCH_COMMAND ${CMAKE_COMMAND} -DSMALL_MAX=${SIM_THEME_SMALL_MAX}
                  -P ${CMAKE_CURRENT_SOURCE_DIR}/patch_lvgl_theme.cmake
)
FetchContent_MakeAvailable(lvgl)

set(MAIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../main)

add_executable(sim
    sim_main.c
    sim_sound.c
    ${MAIN_DIR}/menu_data.c
    ${MAIN_DIR}/menu_nav.c
    ${MAIN_DIR}/ui_screens.c
    ${MAIN_DIR}/fonts/font_ja_menu_14.c
    ${MAIN_DIR}/icons/icon_salt.c
)

if(SIM_UI_SCALE GREATER 1)
    # 高解像度ビルド専用: 56px版フォントと、4倍に作り直したアイコン
    target_sources(sim PRIVATE
        font_ja_menu_56.c
        sim_hires_assets.c
    )
endif()

target_include_directories(sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}   # esp_log.hスタブ + lv_conf.h
    ${MAIN_DIR}
)

target_link_libraries(sim PRIVATE lvgl)

if(EMSCRIPTEN)
    target_link_options(sim PRIVATE
        "-sUSE_SDL=2"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sASYNCIFY"
    )
    set_target_properties(sim PROPERTIES OUTPUT_NAME "index" SUFFIX ".html")
else()
    find_package(SDL2 REQUIRED)
    target_link_libraries(sim PRIVATE SDL2::SDL2)
endif()
