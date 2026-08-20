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
cmake_minimum_required(VERSION 3.16)
project(food_printer_sim C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE PATH "" FORCE)
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
include(FetchContent)
FetchContent_Declare(
    lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v9.5.0
    GIT_SHALLOW TRUE
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
