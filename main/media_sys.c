/* Media system

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "codec_init.h"
#include "codec_board.h"
#if CONFIG_IDF_TARGET_ESP32P4
#include "esp_video_init.h"
#endif
#include "esp_capture_path_simple.h"
#include "esp_capture_audio_enc.h"
#include "av_render.h"
#include "av_render_default.h"
#include "common.h"
#include "esp_log.h"
#include "settings.h"
#include "media_lib_os.h"
#include "esp_timer.h"
#include "esp_audio_enc_default.h"
#include "esp_audio_dec_default.h"
#include "esp_capture_defaults.h"

#define TAG "MEDIA_SYS"

#define RET_ON_NULL(ptr, v) do {                                \
    if (ptr == NULL) {                                          \
        ESP_LOGE(TAG, "Memory allocate fail on %d", __LINE__);  \
        return v;                                               \
    }                                                           \
} while (0)

typedef struct {
    esp_capture_path_handle_t   capture_handle;
    esp_capture_aenc_if_t      *aud_enc;
    esp_capture_audio_src_if_t *aud_src;
    esp_capture_path_if_t      *path_if;
} capture_system_t;

typedef struct {
    audio_render_handle_t audio_render;
    av_render_handle_t    player;
} player_system_t;

static capture_system_t capture_sys;
static player_system_t  player_sys;

static bool           music_playing  = false;
static bool           music_stopping = false;
static const uint8_t *music_to_play;
static int            music_size;
static int            music_duration;


static int build_capture_system(void)
{
    capture_sys.aud_enc = esp_capture_new_audio_encoder();
    RET_ON_NULL(capture_sys.aud_enc, -1);


    esp_capture_audio_codec_src_cfg_t codec_cfg = {
        .record_handle = get_record_handle(),
    };
    capture_sys.aud_src = esp_capture_new_audio_codec_src(&codec_cfg);
    RET_ON_NULL(capture_sys.aud_src, -1);
    esp_capture_simple_path_cfg_t simple_cfg = {
        .aenc = capture_sys.aud_enc,
    };
    capture_sys.path_if = esp_capture_build_simple_path(&simple_cfg);
    RET_ON_NULL(capture_sys.path_if, -1);
    // Create capture system
    esp_capture_cfg_t cfg = {
        .sync_mode = ESP_CAPTURE_SYNC_MODE_AUDIO,
        .audio_src = capture_sys.aud_src,
        .capture_path = capture_sys.path_if,
    };
    esp_capture_open(&cfg, &capture_sys.capture_handle);
    return 0;
}

static int build_player_system()
{
    i2s_render_cfg_t i2s_cfg = {
        .fixed_clock = true,
        .play_handle = get_playback_handle(),
    };
    player_sys.audio_render = av_render_alloc_i2s_render(&i2s_cfg);
    if (player_sys.audio_render == NULL) {
        ESP_LOGE(TAG, "Fail to create audio render");
        return -1;
    }

    av_render_cfg_t render_cfg = {
        .audio_render = player_sys.audio_render,
        .audio_raw_fifo_size = 4096,
        .audio_render_fifo_size = 6 * 1024,
        .allow_drop_data = false,
    };
    player_sys.player = av_render_open(&render_cfg);
    if (player_sys.player == NULL) {
        ESP_LOGE(TAG, "Fail to create player");
        return -1;
    }
    return 0;
}

int media_sys_buildup(void)
{
    // Register for default audio and video codecs
    esp_audio_enc_register_default();

    esp_audio_dec_register_default();
    // Build capture system
    build_capture_system();
    // Build player system
    build_player_system();
    return 0;
}

int media_sys_get_provider(esp_webrtc_media_provider_t *provide)
{
    provide->capture = capture_sys.capture_handle;
    provide->player = player_sys.player;
    return 0;
}

int test_capture_to_player(void)
{
    esp_capture_sink_cfg_t sink_cfg = {
        .audio_info = {
            .codec = ESP_CAPTURE_CODEC_TYPE_G711A,
            .sample_rate = 8000,
            .channel = 1,
            .bits_per_sample = 16,
        },
    };
    // Create capture
    esp_capture_path_handle_t capture_path = NULL;
    esp_capture_setup_path(capture_sys.capture_handle, ESP_CAPTURE_PATH_PRIMARY, &sink_cfg, &capture_path);
    esp_capture_enable_path(capture_path, ESP_CAPTURE_RUN_TYPE_ALWAYS);
    // Create player
    av_render_audio_info_t render_aud_info = {
        .codec = AV_RENDER_AUDIO_CODEC_G711A,
        .sample_rate = 8000,
        .channel = 1,
    };
    av_render_add_audio_stream(player_sys.player, &render_aud_info);



    uint32_t start_time = (uint32_t)(esp_timer_get_time() / 1000);
    esp_capture_start(capture_sys.capture_handle);
    while ((uint32_t)(esp_timer_get_time() / 1000) < start_time + 2000) {
        media_lib_thread_sleep(30);
        esp_capture_stream_frame_t frame = {
            .stream_type = ESP_CAPTURE_STREAM_TYPE_AUDIO,
        };
        while (esp_capture_acquire_path_frame(capture_path, &frame, true) == ESP_CAPTURE_ERR_OK) {
            av_render_audio_data_t audio_data = {
                .data = frame.data,
                .size = frame.size,
                .pts = frame.pts,
            };
            av_render_add_audio_data(player_sys.player, &audio_data);
            esp_capture_release_path_frame(capture_path, &frame);
        }
        frame.stream_type = ESP_CAPTURE_STREAM_TYPE_AUDIO;
        while (esp_capture_acquire_path_frame(capture_path, &frame, true) == ESP_CAPTURE_ERR_OK) {


            esp_capture_release_path_frame(capture_path, &frame);
        }
    }
    esp_capture_stop(capture_sys.capture_handle);
    av_render_reset(player_sys.player);
    return 0;
}

static void music_play_thread(void *arg)
{
    // Suppose all music is AAC
    av_render_audio_info_t render_aud_info = {
        .codec = AV_RENDER_AUDIO_CODEC_AAC,
    };
    av_render_add_audio_stream(player_sys.player, &render_aud_info);
    int music_pos = 0;
    while (!music_stopping && music_duration >= 0) {
        uint32_t start_time = esp_timer_get_time() / 1000;
        int send_size = music_size - music_pos;
        const uint8_t *adts_header = music_to_play + music_pos;
        if (adts_header[0] != 0xFF) {
            send_size = 0;
        } else {
            int frame_size = ((adts_header[3] & 0x03) << 11) | (adts_header[4] << 3) | (adts_header[5] >> 5);
            if (frame_size < send_size) {
                send_size = frame_size;
            }
        }
        if (send_size) {
            av_render_audio_data_t audio_data = {
                .data = (uint8_t *)adts_header,
                .size = send_size,
            };
            int ret = av_render_add_audio_data(player_sys.player, &audio_data);
            if (ret != 0) {
                break;
            }
            music_pos += send_size;
        }
        if (music_pos >= music_size || send_size == 0) {
            music_pos = 0;
            // Play one loop only
            if (music_duration == 0) {
                av_render_fifo_stat_t stat = { 0 };
                while (!music_stopping) {
                    av_render_get_audio_fifo_level(player_sys.player, &stat);
                    if (stat.data_size > 0) {
                        media_lib_thread_sleep(50);
                        continue;
                    }
                    break;
                }
                break;
            }
        }
        uint32_t end_time = esp_timer_get_time() / 1000;
        if (music_duration) {
            music_duration -= end_time - start_time;
        }
    }
    av_render_reset(player_sys.player);
    music_stopping = false;
    music_playing = false;
    media_lib_thread_destroy(NULL);
}

int play_music(const uint8_t *data, int size, int duration)
{
    if (music_playing) {
        ESP_LOGE(TAG, "Music is playing, stop automatically");
        stop_music();
    }
    music_playing = true;
    music_to_play = data;
    music_size = size;
    music_duration = duration;
    media_lib_thread_handle_t thread;
    int ret = media_lib_thread_create_from_scheduler(&thread, "music_player", music_play_thread, NULL);
    if (ret != 0) {
        music_playing = false;
        ESP_LOGE(TAG, "Fail to create music_player thread");
        return ret;
    }
    return 0;
}

int stop_music()
{
    if (music_playing) {
        music_stopping = true;
        while (music_stopping) {
            media_lib_thread_sleep(20);
        }
    }
    return 0;
}
