#include "input_hub.h"
#include "encoder_unit.h"
#include "scroll_unit.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdint.h>

static const char *TAG = "input_hub";

typedef struct {
    const char *name;
    esp_err_t (*init)(void);
    esp_err_t (*poll)(int32_t *out_delta, bool *out_button_pressed);
    bool available;  // init()に成功した
    bool comm_ok;    // 直近のpoll()が成功したか(ログの重複抑止用)
    int     retry_count;    // input_hub_retry_unavailable()での再試行回数
    int64_t last_retry_us;  // 最後に再試行した時刻(esp_timer_get_time()、未試行なら0)
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

/* ---- ランタイム再試行(コールドブート復旧) ----
 * 【2026-11-22 実機ログで確定】USB給電開始直後のコールドブートでは、
 * Grove電源がまだ供給されていないタイミング(ブート後約2.3〜3.2秒)で
 * Encoder/Scroll両方の初期化が失敗し、復旧手段が無いためリセットするまで
 * 操作不能になっていた。電源自体はその後に来る(リセット後は動く)ので、
 * 初期化に失敗した入力は後からinit()をやり直せば復旧できる。
 * その前提として、encoder_init()は失敗時にバス/デバイスを後始末して
 * 再呼び出し可能にしてある(scroll側はソフトI2Cで再呼び出しに支障なし)。
 *
 * 再試行の間隔: 最初のうち(FAST_ATTEMPTS回まで)は1秒ごと。電源が来るのは
 * 起動から数秒〜十数秒の範囲と見込まれるため(ディスプレイ初期化が約9秒
 * 時点。復旧した時刻がログに残るので、電源がいつ来たかの切り分けも兼ねる)。
 * それを超えたら15秒ごとに落とす。本当に未接続の入力を永遠に1秒ごとに
 * 試すと、init()自体の安定待ち+リトライでメインループが毎回ブロックされ
 * (encoderで約0.75秒)、ドライバ側の失敗ログも出続けるため。
 * 1回の呼び出しで再試行する入力は1つだけにし、ループ1周期あたりの
 * ブロック時間を抑える。 */
#define INPUT_HUB_RETRY_INTERVAL_FAST_US  (1LL * 1000 * 1000)
#define INPUT_HUB_RETRY_INTERVAL_SLOW_US  (15LL * 1000 * 1000)
#define INPUT_HUB_RETRY_FAST_ATTEMPTS     15

void input_hub_retry_unavailable(void)
{
    int64_t now = esp_timer_get_time();

    for (int i = 0; i < (int)INPUT_SRC_COUNT; i++) {
        input_entry_t *e = &s_inputs[i];
        if (e->available) {
            continue;
        }
        int64_t interval = (e->retry_count < INPUT_HUB_RETRY_FAST_ATTEMPTS)
                               ? INPUT_HUB_RETRY_INTERVAL_FAST_US
                               : INPUT_HUB_RETRY_INTERVAL_SLOW_US;
        if (e->last_retry_us != 0 && now - e->last_retry_us < interval) {
            continue; // まだ間隔が空いていない
        }

        e->last_retry_us = now;
        e->retry_count++;
        esp_err_t err = e->init();
        if (err == ESP_OK) {
            e->available = true;
            e->comm_ok = true;
            ESP_LOGI(TAG, "%s: 再試行で初期化に成功しました(復旧, %d回目)", e->name, e->retry_count);
        }
        return; // この呼び出しではここまで(1入力だけ)
    }
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
