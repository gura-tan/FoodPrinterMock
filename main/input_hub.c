#include "input_hub.h"
#include "encoder_unit.h"
#include "scroll_unit.h"
#include "esp_log.h"

static const char *TAG = "input_hub";

typedef struct {
    const char *name;
    esp_err_t (*init)(void);
    esp_err_t (*poll)(int32_t *out_delta, bool *out_button_pressed);
    bool available;  // init()に成功した
    bool comm_ok;    // 直近のpoll()が成功したか(ログの重複抑止用)
} input_entry_t;

/* input_source_tの並びと一致させること(添字がそのままinput_source_tになる)。
 * 入力を増やすときはinput_source.hのenumとこの表の両方に足す。 */
static input_entry_t s_inputs[INPUT_SRC_COUNT] = {
    [INPUT_SRC_ENCODER] = { .name = "encoder", .init = encoder_init, .poll = encoder_poll },
    [INPUT_SRC_SCROLL]  = { .name = "scroll",  .init = scroll_init,  .poll = scroll_poll  },
};

static bool valid_src(input_source_t src)
{
    return (int)src >= 0 && (int)src < (int)INPUT_SRC_COUNT;
}

esp_err_t input_hub_init(void)
{
    int available = 0;
    for (int i = 0; i < (int)INPUT_SRC_COUNT; i++) {
        input_entry_t *e = &s_inputs[i];
        esp_err_t err = e->init();
        e->available = (err == ESP_OK);
        e->comm_ok = e->available;
        if (e->available) {
            available++;
            ESP_LOGI(TAG, "%s: 利用可能", e->name);
        } else {
            /* 未接続でも起動は継続する(以降のポーリングは黙ってスキップされる) */
            ESP_LOGW(TAG, "%s: 初期化に失敗しました(%s)。未接続の可能性があるため、この入力なしで継続します",
                     e->name, esp_err_to_name(err));
        }
    }
    return (available > 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

bool input_hub_is_available(input_source_t src)
{
    return valid_src(src) && s_inputs[src].available;
}

esp_err_t input_hub_poll(input_source_t src, int32_t *out_delta, bool *out_button_pressed)
{
    if (!valid_src(src)) {
        return ESP_ERR_INVALID_ARG;
    }
    input_entry_t *e = &s_inputs[src];
    if (!e->available) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = e->poll(out_delta, out_button_pressed);
    if (err != ESP_OK) {
        if (e->comm_ok) {
            ESP_LOGW(TAG, "%s: 通信エラー(%s)。復旧するまで同じ警告は出しません",
                     e->name, esp_err_to_name(err));
            e->comm_ok = false;
        }
    } else if (!e->comm_ok) {
        ESP_LOGI(TAG, "%s: 通信が復旧しました", e->name);
        e->comm_ok = true;
    }
    return err;
}

const char *input_hub_name(input_source_t src)
{
    return valid_src(src) ? s_inputs[src].name : "?";
}
