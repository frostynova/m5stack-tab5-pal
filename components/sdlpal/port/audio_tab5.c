#include "audio.h"
#include "aviplay.h"
#include "palcfg.h"
#include "players.h"
#include "resampler.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define TAB5_AUDIO_SAMPLE_RATE 44100
#define TAB5_AUDIO_CHANNELS 1
#define TAB5_AUDIO_FRAMES 1024
#define TAB5_AUDIO_BYTES (TAB5_AUDIO_FRAMES * TAB5_AUDIO_CHANNELS * sizeof(int16_t))
#define TAB5_CODEC_VOLUME 55

static const char *TAG = "sdlpal_audio";
extern esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void);
static esp_codec_dev_handle_t s_speaker;
static SemaphoreHandle_t s_audio_mutex;
static TaskHandle_t s_audio_task;
static volatile bool s_audio_running;

AUDIODEVICE gAudioDevice = {
    .spec = {
        .format = SDL_AUDIO_S16,
        .channels = TAB5_AUDIO_CHANNELS,
        .freq = TAB5_AUDIO_SAMPLE_RATE,
    },
};

static void adjust_volume(int16_t *samples, int volume, size_t count)
{
    if (volume >= SDL_MIX_MAXVOLUME) {
        return;
    }
    if (volume <= 0) {
        memset(samples, 0, count * sizeof(*samples));
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        samples[i] = (int16_t)((int32_t)samples[i] * volume / SDL_MIX_MAXVOLUME);
    }
}

static void mix_saturating(int16_t *dest, const int16_t *source, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        int32_t mixed = (int32_t)dest[i] + source[i];
        if (mixed > INT16_MAX) {
            mixed = INT16_MAX;
        } else if (mixed < INT16_MIN) {
            mixed = INT16_MIN;
        }
        dest[i] = (int16_t)mixed;
    }
}

static void fill_audio_buffer(int16_t *samples, size_t bytes)
{
    memset(samples, 0, bytes);

    AUDIO_Lock();
    if (gAudioDevice.fMusicEnabled && gAudioDevice.pMusPlayer != NULL &&
        gAudioDevice.iMusicVolume > 0) {
        gAudioDevice.pMusPlayer->FillBuffer(gAudioDevice.pMusPlayer,
                                             (LPBYTE)samples, (INT)bytes);
        adjust_volume(samples, gAudioDevice.iMusicVolume, bytes / sizeof(*samples));
    }

    if (gAudioDevice.fSoundEnabled && gAudioDevice.pSoundPlayer != NULL &&
        gAudioDevice.pSoundBuffer != NULL && gAudioDevice.iSoundVolume > 0) {
        memset(gAudioDevice.pSoundBuffer, 0, bytes);
        gAudioDevice.pSoundPlayer->FillBuffer(gAudioDevice.pSoundPlayer,
                                               gAudioDevice.pSoundBuffer, (INT)bytes);
        adjust_volume((int16_t *)gAudioDevice.pSoundBuffer, gAudioDevice.iSoundVolume,
                      bytes / sizeof(*samples));
        mix_saturating(samples, (const int16_t *)gAudioDevice.pSoundBuffer,
                       bytes / sizeof(*samples));
    }
    AUDIO_Unlock();
}

static void tab5_audio_task(void *arg)
{
    (void)arg;
    int16_t *samples = heap_caps_malloc(TAB5_AUDIO_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (samples == NULL) {
        ESP_LOGE(TAG, "could not allocate %u-byte internal PCM buffer", (unsigned)TAB5_AUDIO_BYTES);
        s_audio_running = false;
        s_audio_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    unsigned consecutive_errors = 0;
    while (s_audio_running) {
        fill_audio_buffer(samples, TAB5_AUDIO_BYTES);
        const int ret = esp_codec_dev_write(s_speaker, samples, TAB5_AUDIO_BYTES);
        if (ret != ESP_CODEC_DEV_OK) {
            if (consecutive_errors++ == 0) {
                ESP_LOGE(TAG, "speaker write failed: %d", ret);
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        } else {
            consecutive_errors = 0;
            /* The codec writer may return as soon as DMA accepts the block.
             * Give the core's idle task a deterministic scheduling window. */
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    free(samples);
    s_audio_task = NULL;
    vTaskDelete(NULL);
}

INT AUDIO_OpenDevice(VOID)
{
    if (gAudioDevice.fOpened) {
        return -1;
    }

    /* Tab5 has one internal speaker. Mixing directly to mono halves the I2S
     * bandwidth and avoids allocating a second channel that cannot be heard. */
    gConfig.iSampleRate = TAB5_AUDIO_SAMPLE_RATE;
    /* The desktop defaults synthesize OPL at 49716 Hz with sinc resampling
     * and dual-chip surround. That is needlessly expensive for Tab5's mono
     * speaker and can starve the task watchdog. Synthesize directly at the
     * device rate with the integer DOSBox core instead. */
    gConfig.iOPLSampleRate = TAB5_AUDIO_SAMPLE_RATE;
    gConfig.iResampleQuality = RESAMPLER_QUALITY_LINEAR;
    gConfig.eOPLCore = OPLCORE_DBINT;
    gConfig.fUseSurroundOPL = FALSE;
    gConfig.iAudioChannels = TAB5_AUDIO_CHANNELS;
    gConfig.wAudioBufferSize = TAB5_AUDIO_FRAMES;
    gAudioDevice.spec.freq = TAB5_AUDIO_SAMPLE_RATE;
    gAudioDevice.spec.format = SDL_AUDIO_S16;
    gAudioDevice.spec.channels = TAB5_AUDIO_CHANNELS;
    gAudioDevice.iMusicVolume = gConfig.iMusicVolume * SDL_MIX_MAXVOLUME / PAL_MAX_VOLUME;
    gAudioDevice.iSoundVolume = gConfig.iSoundVolume * SDL_MIX_MAXVOLUME / PAL_MAX_VOLUME;
    gAudioDevice.fMusicEnabled = TRUE;
    gAudioDevice.fSoundEnabled = TRUE;

    s_audio_mutex = xSemaphoreCreateRecursiveMutex();
    if (s_audio_mutex == NULL) {
        ESP_LOGE(TAG, "could not create audio mutex");
        return -2;
    }

    resampler_init();
    s_speaker = bsp_audio_codec_speaker_init();
    if (s_speaker == NULL) {
        ESP_LOGE(TAG, "ES8388 speaker initialization failed");
        vSemaphoreDelete(s_audio_mutex);
        s_audio_mutex = NULL;
        return -3;
    }

    esp_codec_dev_sample_info_t format = {
        .sample_rate = TAB5_AUDIO_SAMPLE_RATE,
        .channel = TAB5_AUDIO_CHANNELS,
        .bits_per_sample = 16,
    };
    int ret = esp_codec_dev_open(s_speaker, &format);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "ES8388 speaker open failed: %d", ret);
        vSemaphoreDelete(s_audio_mutex);
        s_audio_mutex = NULL;
        return -4;
    }
    esp_codec_dev_set_out_vol(s_speaker, TAB5_CODEC_VOLUME);

    gAudioDevice.pSoundPlayer = SOUND_Init();
    if (gAudioDevice.pSoundPlayer == NULL) {
        ESP_LOGE(TAG, "sounds.mkf/voc.mkf could not be opened");
        esp_codec_dev_close(s_speaker);
        vSemaphoreDelete(s_audio_mutex);
        s_audio_mutex = NULL;
        return -5;
    }

    gAudioDevice.pSoundBuffer = heap_caps_malloc(TAB5_AUDIO_BYTES,
                                                  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (gAudioDevice.pSoundBuffer == NULL) {
        ESP_LOGE(TAG, "could not allocate sound mixing buffer");
        gAudioDevice.pSoundPlayer->Shutdown(gAudioDevice.pSoundPlayer);
        gAudioDevice.pSoundPlayer = NULL;
        esp_codec_dev_close(s_speaker);
        vSemaphoreDelete(s_audio_mutex);
        s_audio_mutex = NULL;
        return -6;
    }

    gAudioDevice.pMusPlayer = RIX_Init(
        UTIL_GetFullPathName(PAL_BUFFER_SIZE_ARGS(0), gConfig.pszGamePath, "mus.mkf"));
    if (gAudioDevice.pMusPlayer == NULL) {
        ESP_LOGW(TAG, "mus.mkf/RIX player initialization failed; continuing with sound effects");
        gAudioDevice.fMusicEnabled = FALSE;
    }

    s_audio_running = true;
    if (xTaskCreatePinnedToCore(tab5_audio_task, "sdlpal_audio", 12288, NULL, 5,
                                &s_audio_task, 1) != pdPASS) {
        ESP_LOGE(TAG, "could not create audio output task");
        s_audio_running = false;
        gAudioDevice.pSoundPlayer->Shutdown(gAudioDevice.pSoundPlayer);
        gAudioDevice.pSoundPlayer = NULL;
        if (gAudioDevice.pMusPlayer != NULL) {
            gAudioDevice.pMusPlayer->Shutdown(gAudioDevice.pMusPlayer);
            gAudioDevice.pMusPlayer = NULL;
        }
        free(gAudioDevice.pSoundBuffer);
        gAudioDevice.pSoundBuffer = NULL;
        esp_codec_dev_close(s_speaker);
        vSemaphoreDelete(s_audio_mutex);
        s_audio_mutex = NULL;
        return -7;
    }

    gAudioDevice.fOpened = TRUE;
    ESP_LOGI(TAG, "ES8388 output ready: %d Hz, mono, 16-bit, %d frames, volume %d%%",
             TAB5_AUDIO_SAMPLE_RATE, TAB5_AUDIO_FRAMES, TAB5_CODEC_VOLUME);
    ESP_LOGI(TAG, "sound effects enabled; RIX/OPL music %s",
             gAudioDevice.pMusPlayer != NULL ? "enabled" : "disabled");
    return 0;
}

BOOL AUDIO_CD_Available(VOID) { return FALSE; }

VOID AUDIO_CloseDevice(VOID)
{
    if (!gAudioDevice.fOpened) {
        return;
    }

    s_audio_running = false;
    for (int i = 0; i < 20 && s_audio_task != NULL; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (gAudioDevice.pSoundPlayer != NULL) {
        gAudioDevice.pSoundPlayer->Shutdown(gAudioDevice.pSoundPlayer);
        gAudioDevice.pSoundPlayer = NULL;
    }
    if (gAudioDevice.pMusPlayer != NULL) {
        gAudioDevice.pMusPlayer->Shutdown(gAudioDevice.pMusPlayer);
        gAudioDevice.pMusPlayer = NULL;
    }
    if (gAudioDevice.pSoundBuffer != NULL) {
        free(gAudioDevice.pSoundBuffer);
        gAudioDevice.pSoundBuffer = NULL;
    }
    if (s_speaker != NULL) {
        esp_codec_dev_close(s_speaker);
    }
    if (s_audio_mutex != NULL) {
        vSemaphoreDelete(s_audio_mutex);
        s_audio_mutex = NULL;
    }
    gAudioDevice.fOpened = FALSE;
}

SDL_AudioSpec *AUDIO_GetDeviceSpec(VOID) { return &gAudioDevice.spec; }

static void change_volume(int delta)
{
    gConfig.iMusicVolume = SDL_clamp(gConfig.iMusicVolume + delta, 0, PAL_MAX_VOLUME);
    gConfig.iSoundVolume = SDL_clamp(gConfig.iSoundVolume + delta, 0, PAL_MAX_VOLUME);
    gAudioDevice.iMusicVolume = gConfig.iMusicVolume * SDL_MIX_MAXVOLUME / PAL_MAX_VOLUME;
    gAudioDevice.iSoundVolume = gConfig.iSoundVolume * SDL_MIX_MAXVOLUME / PAL_MAX_VOLUME;
}

VOID AUDIO_IncreaseVolume(VOID) { change_volume(3); }
VOID AUDIO_DecreaseVolume(VOID) { change_volume(-3); }

VOID AUDIO_Tab5AdjustVolume(INT delta) { change_volume(delta); }

INT AUDIO_Tab5GetVolume(VOID)
{
    return (gConfig.iMusicVolume + gConfig.iSoundVolume + 1) / 2;
}

VOID AUDIO_PlayMusic(INT music, BOOL loop, FLOAT fade_time)
{
    AUDIO_Lock();
    if (gAudioDevice.pMusPlayer != NULL) {
        gAudioDevice.pMusPlayer->Play(gAudioDevice.pMusPlayer, music, loop, fade_time);
    }
    AUDIO_Unlock();
}

BOOL AUDIO_PlayCDTrack(INT track)
{
    (void)track;
    return FALSE;
}

VOID AUDIO_PlaySound(INT sound)
{
    if (gAudioDevice.pSoundPlayer != NULL) {
        gAudioDevice.pSoundPlayer->Play(gAudioDevice.pSoundPlayer, abs(sound), FALSE, 0.0f);
    }
}

VOID AUDIO_EnableMusic(BOOL enable) { gAudioDevice.fMusicEnabled = enable; }
BOOL AUDIO_MusicEnabled(VOID) { return gAudioDevice.fMusicEnabled; }
VOID AUDIO_EnableSound(BOOL enable) { gAudioDevice.fSoundEnabled = enable; }
BOOL AUDIO_SoundEnabled(VOID) { return gAudioDevice.fSoundEnabled; }

void AUDIO_Lock(void)
{
    if (s_audio_mutex != NULL) {
        xSemaphoreTakeRecursive(s_audio_mutex, portMAX_DELAY);
    }
}

void AUDIO_Unlock(void)
{
    if (s_audio_mutex != NULL) {
        xSemaphoreGiveRecursive(s_audio_mutex);
    }
}

VOID PAL_AVIInit(VOID) {}
VOID PAL_AVIShutdown(VOID) {}
BOOL PAL_PlayAVI(const char *path)
{
    (void)path;
    return FALSE;
}
void SDLCALL AVI_FillAudioBuffer(void *udata, uint8_t *stream, int len)
{
    (void)udata;
    (void)stream;
    (void)len;
}
void *AVI_GetPlayState(void) { return NULL; }
