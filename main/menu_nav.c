#include "menu_nav.h"
#include <stdio.h>

#define MAX_OPTIONS         32   // ローラーに並べる選択肢の最大数(数値レンジもここに収める)
#define OPTION_STR_LEN       24
#define MAX_PARAMS_PER_MENU   8

static nav_state_t s_state;
static bool        s_dirty = true;
static int         s_current_selection;

static size_t      s_option_count;
static char         s_option_text[MAX_OPTIONS][OPTION_STR_LEN];
static const char  *s_option_ptr[MAX_OPTIONS];

static int32_t s_param_values[MAX_PARAMS_PER_MENU];

static const menu_item_def_t *current_menu_item(void)
{
    return &g_major_categories[s_state.major_index]
                .sub_categories[s_state.sub_index]
                .menu_items[s_state.menu_index];
}

/* パラメータのmin〜maxをMAX_OPTIONS個以内に収めるための刻み幅 */
static int32_t param_step(const parameter_def_t *p)
{
    int32_t range = p->max_value - p->min_value;
    if (range <= 0) return 1;
    if (range <= (MAX_OPTIONS - 1)) return 1;
    return (range + (MAX_OPTIONS - 2)) / (MAX_OPTIONS - 1);
}

static void refresh_options(void)
{
    s_current_selection = 0;

    switch (s_state.level) {
    case NAV_LEVEL_MAJOR: {
        s_option_count = g_major_category_count;
        for (size_t i = 0; i < s_option_count && i < MAX_OPTIONS; i++) {
            s_option_ptr[i] = g_major_categories[i].name;
        }
        break;
    }
    case NAV_LEVEL_SUB: {
        const major_category_def_t *maj = &g_major_categories[s_state.major_index];
        if (maj->sub_category_count == 0) {
            snprintf(s_option_text[0], OPTION_STR_LEN, "(準備中)");
            s_option_ptr[0] = s_option_text[0];
            s_option_count = 1;
        } else {
            s_option_count = maj->sub_category_count;
            for (size_t i = 0; i < s_option_count && i < MAX_OPTIONS; i++) {
                s_option_ptr[i] = maj->sub_categories[i].name;
            }
        }
        break;
    }
    case NAV_LEVEL_MENU: {
        const sub_category_def_t *sub =
            &g_major_categories[s_state.major_index].sub_categories[s_state.sub_index];
        s_option_count = sub->menu_item_count;
        for (size_t i = 0; i < s_option_count && i < MAX_OPTIONS; i++) {
            s_option_ptr[i] = sub->menu_items[i].name;
        }
        break;
    }
    case NAV_LEVEL_PARAM: {
        const menu_item_def_t *menu = current_menu_item();
        if (s_state.parameter_index >= (int)menu->parameter_count) {
            /* START(仮想ステップ): 選べる値は無いので、ダイヤルを回しても
             * 何も起きないよう選択肢を1件だけにしておく */
            snprintf(s_option_text[0], OPTION_STR_LEN, "START");
            s_option_ptr[0] = s_option_text[0];
            s_option_count = 1;
            break;
        }
        const parameter_def_t *p = &menu->parameters[s_state.parameter_index];
        int32_t step = param_step(p);
        size_t count = 0;
        for (int32_t v = p->min_value; v <= p->max_value && count < MAX_OPTIONS; v += step) {
            snprintf(s_option_text[count], OPTION_STR_LEN, "%s %ld%s", p->name, (long)v, p->unit);
            s_option_ptr[count] = s_option_text[count];
            count++;
        }
        s_option_count = count;
        break;
    }
    }

    s_dirty = false;
}

void nav_init(void)
{
    s_state.level = NAV_LEVEL_MAJOR;
    s_state.major_index = 0;
    s_state.sub_index = 0;
    s_state.menu_index = 0;
    s_state.parameter_index = 0;
    s_dirty = true;
}

const nav_state_t *nav_get_state(void)
{
    return &s_state;
}

const char *const *nav_get_current_options(size_t *out_count)
{
    if (s_dirty) {
        refresh_options();
    }
    if (out_count) {
        *out_count = s_option_count;
    }
    return s_option_ptr;
}

void nav_move_selection(int delta)
{
    if (s_dirty) {
        refresh_options();
    }
    if (s_option_count == 0) {
        return;
    }
    int next = s_current_selection + delta;
    if (next < 0) next = 0;
    if (next >= (int)s_option_count) next = (int)s_option_count - 1;
    s_current_selection = next;
}

int nav_get_selected_index(void)
{
    return s_current_selection;
}

bool nav_confirm(void)
{
    if (s_dirty) {
        refresh_options();
    }

    switch (s_state.level) {
    case NAV_LEVEL_MAJOR: {
        if (g_major_categories[s_current_selection].sub_category_count == 0) {
            /* 「(準備中)」を決定しても先に進めない(データが無いので当然戻り値なし) */
            return false;
        }
        s_state.major_index = s_current_selection;
        s_state.level = NAV_LEVEL_SUB;
        s_dirty = true;
        return false;
    }
    case NAV_LEVEL_SUB: {
        s_state.sub_index = s_current_selection;
        s_state.level = NAV_LEVEL_MENU;
        s_dirty = true;
        return false;
    }
    case NAV_LEVEL_MENU: {
        s_state.menu_index = s_current_selection;
        s_state.level = NAV_LEVEL_PARAM;
        s_state.parameter_index = 0;
        s_dirty = true;
        return false;
    }
    case NAV_LEVEL_PARAM: {
        const menu_item_def_t *menu = current_menu_item();
        if (s_state.parameter_index >= (int)menu->parameter_count) {
            /* STARTを確定 = 一連の操作フロー完了。値は持たないので何も保存しない */
            return true;
        }

        const parameter_def_t *p = &menu->parameters[s_state.parameter_index];
        int32_t value = p->min_value + (int32_t)s_current_selection * param_step(p);
        if (value > p->max_value) value = p->max_value;
        if (s_state.parameter_index < MAX_PARAMS_PER_MENU) {
            s_param_values[s_state.parameter_index] = value;
        }

        s_state.parameter_index++; // 次のパラメータ、またはSTART(parameter_index==parameter_count)へ
        s_dirty = true;
        return false;
    }
    }
    return false;
}

void nav_back(void)
{
    switch (s_state.level) {
    case NAV_LEVEL_MAJOR:
        /* 最上位: 何もしない */
        break;
    case NAV_LEVEL_SUB:
        s_state.level = NAV_LEVEL_MAJOR;
        s_dirty = true;
        break;
    case NAV_LEVEL_MENU:
        s_state.level = NAV_LEVEL_SUB;
        s_dirty = true;
        break;
    case NAV_LEVEL_PARAM:
        if (s_state.parameter_index > 0) {
            s_state.parameter_index--;
        } else {
            s_state.level = NAV_LEVEL_MENU;
        }
        s_dirty = true;
        break;
    }
}

int32_t nav_get_confirmed_param_value(int index)
{
    if (index < 0 || index >= MAX_PARAMS_PER_MENU) {
        return 0;
    }
    return s_param_values[index];
}

const menu_item_def_t *nav_get_current_menu_item(void)
{
    return current_menu_item();
}

int32_t nav_get_param_live_value(int index)
{
    if (index < 0 || index >= MAX_PARAMS_PER_MENU) {
        return 0;
    }
    if (s_state.level == NAV_LEVEL_PARAM && index == s_state.parameter_index) {
        if (s_dirty) {
            refresh_options();
        }
        const menu_item_def_t *menu = current_menu_item();
        if (index < (int)menu->parameter_count) {
            const parameter_def_t *p = &menu->parameters[index];
            int32_t value = p->min_value + (int32_t)s_current_selection * param_step(p);
            if (value > p->max_value) value = p->max_value;
            return value;
        }
    }
    return s_param_values[index];
}
