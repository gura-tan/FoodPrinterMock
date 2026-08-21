/*
 * main/sound_hooks.h と同じAPI(sound_hooks_init/play/play_chained)を、
 * ブラウザ上のWeb Audioで実装したもの。main/sound_hooks.cは
 * esp_codec_dev/SDカードに依存するためsim側では使わず、こちらに差し替える。
 *
 * 実機はSDカードの /sdcard/sounds/<preset>/{hit,proceed,move,back,done}.wav を
 * 起動時に1回だけ読み込むが、ブラウザはローカルファイルへ自由にアクセス
 * できないため、ページ上の「音フォルダを選択」ボタンから
 * <input type=file webkitdirectory> でユーザーにPC上の任意のフォルダを
 * 選んでもらい、同名のファイルをWeb Audioでデコードして保持する。
 */
#include "sound_hooks.h"

#if !defined(__EMSCRIPTEN__)
/* ネイティブ(SDL2デスクトップ)ビルド用の何もしないフォールバック。
 * ブラウザ向けWeb Audio実装(EM_JS)はEmscriptenでのみビルドできるため。 */
void sound_hooks_init(void) {}
void sound_hooks_play(ui_sound_id_t id) { (void)id; }
void sound_hooks_play_chained(ui_sound_id_t id) { (void)id; }

#else
#include <emscripten.h>

EM_JS(void, sim_sound_js_init, (), {
    if (window.__simSound) return;

    var state = {
        ctx: null,
        buffers: {},    // "hit"|"proceed"|"move"|"back"|"deny"|"done" -> AudioBuffer
        current: null,  // 再生中のAudioBufferSourceNode(即時割り込み版のみ管理)
        pending: null,  // play_chained()で予約された次の音の名前
        names: ["hit", "proceed", "move", "back", "deny", "done"],
    };
    window.__simSound = state;

    function ensureCtx() {
        if (!state.ctx) {
            state.ctx = new (window.AudioContext || window.webkitAudioContext)();
        }
        if (state.ctx.state === "suspended") {
            state.ctx.resume();
        }
        return state.ctx;
    }

    function playNow(name) {
        var buf = state.buffers[name];
        if (!buf) return;
        var ctx = ensureCtx();
        var src = ctx.createBufferSource();
        src.buffer = buf;
        src.connect(ctx.destination);
        src.onended = function() {
            if (state.current !== src) return;
            state.current = null;
            if (state.pending) {
                var next = state.pending;
                state.pending = null;
                playNow(next);
            }
        };
        src.start();
        state.current = src;
    }

    window.__simSoundPlay = function(name, chained) {
        if (!state.buffers[name]) return;
        if (chained) {
            if (state.current) {
                state.pending = name; // 再生が終わるまで待つ。新しい要求で上書きされる
            } else {
                playNow(name);
            }
        } else {
            state.pending = null; // 割り込み版が来たら予約中のchainedは破棄
            if (state.current) {
                try { state.current.stop(); } catch (e) { /* 再生完了直後などは無視 */ }
            }
            playNow(name);
        }
    };

    var bar = document.createElement('div');
    bar.style.cssText = 'font:13px sans-serif;padding:6px 8px;background:#eee;' +
        'border-bottom:1px solid #ccc;display:flex;align-items:center;gap:8px;';

    var label = document.createElement('span');
    label.textContent = '音: 未読み込み(hit/proceed/move/back/deny/done.wavを含む' +
        'フォルダを選んでください)';

    var btn = document.createElement('button');
    btn.textContent = '音フォルダを選択';

    var input = document.createElement('input');
    input.type = 'file';
    input.webkitdirectory = true;
    input.directory = true;
    input.multiple = true;
    input.style.display = 'none';

    btn.onclick = function() {
        ensureCtx();
        input.click();
    };

    input.onchange = function() {
        ensureCtx();
        var loaded = [];
        var files = Array.prototype.slice.call(input.files);
        var remaining = files.length;
        if (remaining === 0) {
            label.textContent = '音: 選択されたファイルがありません';
            return;
        }
        files.forEach(function(f) {
            var base = f.name.toLowerCase();
            var match = state.names.find(function(n) { return base === n + '.wav'; });
            var done = function() {
                remaining--;
                if (remaining === 0) {
                    label.textContent = '音: ' + loaded.length + '/6 読み込み済み (' +
                        loaded.sort().join(', ') + ')';
                }
            };
            if (!match) { done(); return; }
            f.arrayBuffer().then(function(buf) {
                return state.ctx.decodeAudioData(buf);
            }).then(function(decoded) {
                state.buffers[match] = decoded;
                loaded.push(match);
                done();
            }).catch(function(e) {
                console.warn('sim sound: failed to decode', f.name, e);
                done();
            });
        });
    };

    bar.appendChild(label);
    bar.appendChild(btn);
    bar.appendChild(input);
    document.body.insertBefore(bar, document.body.firstChild);
});

EM_JS(void, sim_sound_js_play, (int id, int chained), {
    var names = ["hit", "proceed", "move", "back", "deny", "done"];
    var name = names[id];
    if (window.__simSoundPlay) {
        window.__simSoundPlay(name, chained);
    }
});

void sound_hooks_init(void)
{
    sim_sound_js_init();
}

void sound_hooks_play(ui_sound_id_t id)
{
    sim_sound_js_play((int)id, 0);
}

void sound_hooks_play_chained(ui_sound_id_t id)
{
    sim_sound_js_play((int)id, 1);
}

#endif /* __EMSCRIPTEN__ */
