#include "audio.h"
#include "aviplay.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "sdlpal_audio";

AUDIODEVICE gAudioDevice = {
    .spec = {
        .format = SDL_AUDIO_S16,
        .channels = 2,
        .freq = 44100,
    },
    .fMusicEnabled = FALSE,
    .fSoundEnabled = FALSE,
};

INT AUDIO_OpenDevice(VOID)
{
    ESP_LOGI(TAG, "audio disabled for the first engine milestone");
    return 0;
}

BOOL AUDIO_CD_Available(VOID) { return FALSE; }
VOID AUDIO_CloseDevice(VOID) {}
SDL_AudioSpec *AUDIO_GetDeviceSpec(VOID) { return &gAudioDevice.spec; }
VOID AUDIO_IncreaseVolume(VOID) {}
VOID AUDIO_DecreaseVolume(VOID) {}
VOID AUDIO_PlayMusic(INT music, BOOL loop, FLOAT fade_time)
{
    (void)music;
    (void)loop;
    (void)fade_time;
}
BOOL AUDIO_PlayCDTrack(INT track)
{
    (void)track;
    return FALSE;
}
VOID AUDIO_PlaySound(INT sound) { (void)sound; }
VOID AUDIO_EnableMusic(BOOL enable) { gAudioDevice.fMusicEnabled = enable; }
BOOL AUDIO_MusicEnabled(VOID) { return gAudioDevice.fMusicEnabled; }
VOID AUDIO_EnableSound(BOOL enable) { gAudioDevice.fSoundEnabled = enable; }
BOOL AUDIO_SoundEnabled(VOID) { return gAudioDevice.fSoundEnabled; }
void AUDIO_Lock(void) {}
void AUDIO_Unlock(void) {}

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
    if (stream != NULL && len > 0) {
        memset(stream, 0, (size_t)len);
    }
}
void *AVI_GetPlayState(void) { return NULL; }
