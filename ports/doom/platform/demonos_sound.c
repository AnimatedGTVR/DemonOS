#include "doomtype.h"
#include "i_sound.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"

#include <demon/c_app.h>
#include <stdint.h>
#include <stdio.h>

#define MIX_CHANNELS 8
#define MIX_FRAMES 1260
#define INVALID_HANDLE UINT64_MAX

struct mix_channel {
    const uint8_t *samples;
    uint32_t count;
    uint32_t position;
    uint32_t step;
    int volume;
    int separation;
    boolean playing;
};

static struct mix_channel channels[MIX_CHANNELS];
static int16_t output[MIX_FRAMES * 2];
static uint64_t audio_handle = INVALID_HANDLE;
static boolean use_prefix;
static uint64_t submitted_buffers;

/* Chocolate Doom keeps these compatibility settings when sound is enabled;
   the native fixed-point mixer does not require an external resampler. */
int use_libsamplerate;
float libsamplerate_scale = 1.0f;

static uint16_t little16(const uint8_t *bytes) {
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8u);
}
static uint32_t little32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) |
        ((uint32_t)bytes[2] << 16u) | ((uint32_t)bytes[3] << 24u);
}
static int clamp16(int value) {
    if (value < -32768) return -32768;
    if (value > 32767) return 32767;
    return value;
}

static boolean sound_init(boolean use_sfx_prefix) {
    use_prefix = use_sfx_prefix;
    audio_handle = demon_service_open(11u);
    for (int index = 0; index < MIX_CHANNELS; ++index)
        channels[index].playing = false;
    if (audio_handle == INVALID_HANDLE) return false;
    printf("DOOM_AUDIO_MIXER_READY rate=44100 channels=2 voices=%i\n",
           MIX_CHANNELS);
    return true;
}

static void sound_shutdown(void) {
    if (audio_handle != INVALID_HANDLE) (void)demon_handle_close(audio_handle);
    audio_handle = INVALID_HANDLE;
}

static int sound_lump(sfxinfo_t *sfx) {
    if (sfx->link != NULL) sfx = sfx->link;
    char name[9];
    if (use_prefix) M_snprintf(name, sizeof(name), "ds%s", sfx->name);
    else M_snprintf(name, sizeof(name), "%s", sfx->name);
    return W_GetNumForName(name);
}

static void sound_params(int handle, int volume, int separation) {
    if (handle < 0 || handle >= MIX_CHANNELS) return;
    channels[handle].volume = volume;
    channels[handle].separation = separation;
}

static int sound_start(sfxinfo_t *sfx, int channel, int volume, int separation) {
    if (channel < 0 || channel >= MIX_CHANNELS || sfx == NULL ||
        sfx->lumpnum < 0) return -1;
    const uint32_t lump_size = (uint32_t)W_LumpLength((unsigned)sfx->lumpnum);
    const uint8_t *lump = W_CacheLumpNum(sfx->lumpnum, PU_STATIC);
    if (lump == NULL || lump_size <= 8u || little16(lump) != 3u) return -1;
    uint32_t count = little32(lump + 4u);
    if (count > lump_size - 8u) count = lump_size - 8u;
    const uint32_t rate = little16(lump + 2u);
    if (rate == 0u || count == 0u) return -1;
    channels[channel] = (struct mix_channel) {
        .samples = lump + 8u,
        .count = count,
        .position = 0u,
        .step = (uint32_t)(((uint64_t)rate << 16u) / 44100u),
        .volume = volume,
        .separation = separation,
        .playing = true,
    };
    if (channels[channel].step == 0u) channels[channel].step = 1u;
    return channel;
}

static void sound_stop(int handle) {
    if (handle >= 0 && handle < MIX_CHANNELS) channels[handle].playing = false;
}
static boolean sound_playing(int handle) {
    return handle >= 0 && handle < MIX_CHANNELS && channels[handle].playing;
}

static void sound_update(void) {
    if (audio_handle == INVALID_HANDLE) return;
    boolean active = false;
    for (int voice = 0; voice < MIX_CHANNELS; ++voice)
        active = active || channels[voice].playing;
    if (!active) return;
    for (uint32_t frame = 0u; frame < MIX_FRAMES; ++frame) {
        int left = 0;
        int right = 0;
        for (int voice = 0; voice < MIX_CHANNELS; ++voice) {
            struct mix_channel *channel = &channels[voice];
            if (!channel->playing) continue;
            const uint32_t sample_index = channel->position >> 16u;
            if (sample_index >= channel->count) {
                channel->playing = false;
                continue;
            }
            const int sample = ((int)channel->samples[sample_index] - 128) << 8;
            const int left_gain = (254 - channel->separation) * channel->volume;
            const int right_gain = channel->separation * channel->volume;
            left += sample * left_gain / (254 * 127);
            right += sample * right_gain / (254 * 127);
            channel->position += channel->step;
        }
        output[frame * 2u] = (int16_t)clamp16(left);
        output[frame * 2u + 1u] = (int16_t)clamp16(right);
    }
    if (demon_audio_submit(audio_handle, output, MIX_FRAMES) == MIX_FRAMES) {
        ++submitted_buffers;
        if (submitted_buffers == 1u) printf("DOOM_AUDIO_FIRST_BUFFER frames=%u\n",
                                            (unsigned)MIX_FRAMES);
    }
}

static void sound_precache(sfxinfo_t *sounds, int count) {
    (void)sounds;
    (void)count;
}

static snddevice_t devices[] = { SNDDEVICE_SB };
sound_module_t DG_sound_module = {
    devices, 1, sound_init, sound_shutdown, sound_lump, sound_update,
    sound_params, sound_start, sound_stop, sound_playing, sound_precache,
};

static boolean music_init(void) { return false; }
static void music_void(void) {}
static void music_volume(int volume) { (void)volume; }
static void *music_register(void *data, int length) {
    (void)data; (void)length; return NULL;
}
static void music_unregister(void *handle) { (void)handle; }
static void music_play(void *handle, boolean looping) {
    (void)handle; (void)looping;
}
static boolean music_playing(void) { return false; }
music_module_t DG_music_module = {
    devices, 1, music_init, music_void, music_volume, music_void, music_void,
    music_register, music_unregister, music_play, music_void, music_playing, NULL,
};
