#include "sound_hooks.h"
#include "esp_log.h"

static const char *TAG = "sound_hooks";

void sound_hooks_init(void)
{
    /* TODO: bsp_audio_codec_speaker_init() や、PSRAM常駐PCMバッファの
     * 初期化をここに実装する(ハードウェア検討メモ「試作0」→音まわりの章を参照) */
    ESP_LOGI(TAG, "sound_hooks_init (stub)");
}

void sound_hooks_play(ui_sound_id_t id)
{
    /* TODO: esp_codec_dev_write() でPSRAM上のPCMサンプルを再生する処理に
     * 置き換える。発音を担当するのはLVGLタスクとは別のFreeRTOSタスクに
     * すること(LVGLはスレッドセーフでないため、音声タスクからlv_*系APIを
     * 直接呼ばないこと)。 */
    static const char *names[] = { "MOVE", "CONFIRM", "BACK", "DONE" };
    ESP_LOGI(TAG, "play sound: %s (stub, 実際の音はまだ未接続)", names[id]);
}
