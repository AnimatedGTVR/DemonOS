#include "audio.h"

#include <demon/c_app.h>
#include <string.h>

#define AUDIO_SERVICE 11u
#define INVALID_HANDLE UINT64_MAX
#define SELFTEST_FRAMES 256u

static uint16_t read_u16(const uint8_t *bytes) {
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8u);
}

static uint32_t read_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) |
        ((uint32_t)bytes[2] << 16u) | ((uint32_t)bytes[3] << 24u);
}

static bool tag_is(const uint8_t *bytes, const char *tag) {
    return bytes[0] == (uint8_t)tag[0] && bytes[1] == (uint8_t)tag[1] &&
        bytes[2] == (uint8_t)tag[2] && bytes[3] == (uint8_t)tag[3];
}

bool DemonButterscotchAudio_decodePcmWav(const uint8_t *bytes,
                                         uint32_t byteCount,
                                         int16_t *stereoOutput,
                                         uint32_t outputCapacityFrames,
                                         uint32_t *outputFrames,
                                         DemonButterscotchPcmStats *stats) {
    if (bytes == NULL || stereoOutput == NULL || outputFrames == NULL ||
        stats == NULL || byteCount < 12u || !tag_is(bytes, "RIFF") ||
        !tag_is(bytes + 8u, "WAVE") || read_u32(bytes + 4u) + 8u > byteCount)
        return false;
    uint32_t position = 12u, rate = 0u, dataOffset = 0u, dataSize = 0u;
    uint16_t format = 0u, channels = 0u, bits = 0u, blockAlign = 0u;
    while (position + 8u <= byteCount) {
        const uint32_t size = read_u32(bytes + position + 4u);
        const uint64_t payload = (uint64_t)position + 8u;
        const uint64_t paddedEnd = payload + size + (size & 1u);
        if (paddedEnd > byteCount) return false;
        if (tag_is(bytes + position, "fmt ")) {
            if (size < 16u) return false;
            format = read_u16(bytes + payload);
            channels = read_u16(bytes + payload + 2u);
            rate = read_u32(bytes + payload + 4u);
            blockAlign = read_u16(bytes + payload + 12u);
            bits = read_u16(bytes + payload + 14u);
        } else if (tag_is(bytes + position, "data")) {
            dataOffset = (uint32_t)payload;
            dataSize = size;
        }
        position = (uint32_t)paddedEnd;
    }
    if (format != 1u || (channels != 1u && channels != 2u) ||
        (bits != 8u && bits != 16u) || rate == 0u || dataSize == 0u ||
        blockAlign != channels * (bits / 8u) || dataSize % blockAlign != 0u)
        return false;
    const uint32_t sourceFrames = dataSize / blockAlign;
    const uint64_t requested = ((uint64_t)sourceFrames * 44100u + rate - 1u) /
        rate;
    if (requested == 0u || requested > outputCapacityFrames ||
        requested > UINT32_MAX) return false;
    for (uint32_t frame = 0u; frame < (uint32_t)requested; ++frame) {
        uint32_t sourceFrame = (uint32_t)(((uint64_t)frame * rate) / 44100u);
        if (sourceFrame >= sourceFrames) sourceFrame = sourceFrames - 1u;
        const uint8_t *sample = bytes + dataOffset + sourceFrame * blockAlign;
        int16_t left, right;
        if (bits == 8u) {
            left = (int16_t)(((int32_t)sample[0] - 128) * 256);
            right = channels == 2u ?
                (int16_t)(((int32_t)sample[1] - 128) * 256) : left;
        } else {
            left = (int16_t)read_u16(sample);
            right = channels == 2u ? (int16_t)read_u16(sample + 2u) : left;
        }
        stereoOutput[frame * 2u] = left;
        stereoOutput[frame * 2u + 1u] = right;
    }
    stats->sourceRate = rate;
    stats->sourceFrames = sourceFrames;
    stats->outputFrames = (uint32_t)requested;
    stats->channels = channels;
    stats->bitsPerSample = bits;
    *outputFrames = (uint32_t)requested;
    return true;
}

bool DemonButterscotchAudio_mixStereo(int16_t *destination,
                                      uint32_t destinationFrames,
                                      const int16_t *source,
                                      uint32_t sourceFrames) {
    if (destination == NULL || source == NULL ||
        sourceFrames > destinationFrames) return false;
    for (uint32_t i = 0u; i < sourceFrames * 2u; ++i) {
        int32_t mixed = (int32_t)destination[i] + source[i];
        if (mixed > 32767) mixed = 32767;
        if (mixed < -32768) mixed = -32768;
        destination[i] = (int16_t)mixed;
    }
    return true;
}

bool DemonButterscotchAudio_pcmSelfTest(DemonButterscotchPcmStats *stats) {
    if (stats == NULL) return false;
    const uint8_t wav[] = {
        'R','I','F','F', 40,0,0,0, 'W','A','V','E',
        'f','m','t',' ', 16,0,0,0, 1,0, 1,0,
        0x22,0x56,0,0, 0x22,0x56,0,0, 1,0, 8,0,
        'd','a','t','a', 4,0,0,0, 0,64,192,255
    };
    int16_t decoded[16] = {0};
    int16_t mixed[16] = {0};
    uint32_t frames = 0u;
    if (!DemonButterscotchAudio_decodePcmWav(wav, sizeof(wav), decoded, 8u,
            &frames, stats) || frames != 8u || decoded[0] != -32768 ||
        decoded[1] != -32768 || decoded[14] != 32512 ||
        !DemonButterscotchAudio_mixStereo(mixed, 8u, decoded, frames) ||
        mixed[14] != 32512) return false;
    mixed[14] = 30000;
    decoded[14] = 30000;
    return DemonButterscotchAudio_mixStereo(mixed, 8u, decoded, frames) &&
        mixed[14] == 32767;
}

void DemonButterscotchMixer_init(DemonButterscotchMixer *mixer) {
    if (mixer == NULL) return;
    memset(mixer, 0, sizeof(*mixer));
    mixer->nextId = 1u;
}

uint32_t DemonButterscotchMixer_play(DemonButterscotchMixer *mixer,
                                     const int16_t *samples,
                                     uint32_t frames, bool loop,
                                     uint16_t gainQ8, int16_t panQ8) {
    if (mixer == NULL || samples == NULL || frames == 0u || gainQ8 > 256u ||
        panQ8 < -256 || panQ8 > 256) return 0u;
    for (uint32_t i = 0u; i < DEMON_BUTTERSCOTCH_VOICE_MAX; ++i) {
        DemonButterscotchVoice *voice = &mixer->voices[i];
        if (voice->active) continue;
        uint32_t id = mixer->nextId++;
        if (id == 0u) id = mixer->nextId++;
        *voice = (DemonButterscotchVoice){
            .samples = samples, .frameCount = frames, .id = id,
            .gainQ8 = gainQ8, .panQ8 = panQ8, .active = true, .loop = loop
        };
        return id;
    }
    return 0u;
}

static DemonButterscotchVoice *find_voice(DemonButterscotchMixer *mixer,
                                          uint32_t voiceId) {
    if (mixer == NULL || voiceId == 0u) return NULL;
    for (uint32_t i = 0u; i < DEMON_BUTTERSCOTCH_VOICE_MAX; ++i)
        if (mixer->voices[i].active && mixer->voices[i].id == voiceId)
            return &mixer->voices[i];
    return NULL;
}

bool DemonButterscotchMixer_stop(DemonButterscotchMixer *mixer,
                                 uint32_t voiceId) {
    DemonButterscotchVoice *voice = find_voice(mixer, voiceId);
    if (voice == NULL) return false;
    voice->active = false;
    return true;
}

bool DemonButterscotchMixer_pause(DemonButterscotchMixer *mixer,
                                  uint32_t voiceId, bool paused) {
    DemonButterscotchVoice *voice = find_voice(mixer, voiceId);
    if (voice == NULL) return false;
    voice->paused = paused;
    return true;
}

static int16_t clamp_sample(int32_t value, uint32_t *clipped) {
    if (value > 32767) {
        if (clipped != NULL) ++*clipped;
        return 32767;
    }
    if (value < -32768) {
        if (clipped != NULL) ++*clipped;
        return -32768;
    }
    return (int16_t)value;
}

bool DemonButterscotchMixer_render(DemonButterscotchMixer *mixer,
                                   int16_t *output, uint32_t frames,
                                   DemonButterscotchMixerStats *stats) {
    if (mixer == NULL || output == NULL || frames == 0u || stats == NULL)
        return false;
    memset(output, 0, (size_t)frames * 2u * sizeof(*output));
    for (uint32_t i = 0u; i < DEMON_BUTTERSCOTCH_VOICE_MAX; ++i) {
        DemonButterscotchVoice *voice = &mixer->voices[i];
        if (!voice->active) continue;
        if (voice->paused) {
            stats->pausedFrames += frames;
            continue;
        }
        for (uint32_t frame = 0u; frame < frames && voice->active; ++frame) {
            if (voice->position == voice->frameCount) {
                if (voice->loop) {
                    voice->position = 0u;
                    ++stats->loopWraps;
                } else {
                    voice->active = false;
                    break;
                }
            }
            const int32_t leftGain = (int32_t)voice->gainQ8 *
                (voice->panQ8 > 0 ? 256 - voice->panQ8 : 256) / 256;
            const int32_t rightGain = (int32_t)voice->gainQ8 *
                (voice->panQ8 < 0 ? 256 + voice->panQ8 : 256) / 256;
            const uint32_t source = voice->position++ * 2u;
            output[frame * 2u] = clamp_sample((int32_t)output[frame * 2u] +
                (int32_t)voice->samples[source] * leftGain / 256,
                &stats->clippedSamples);
            output[frame * 2u + 1u] = clamp_sample(
                (int32_t)output[frame * 2u + 1u] +
                (int32_t)voice->samples[source + 1u] * rightGain / 256,
                &stats->clippedSamples);
        }
    }
    return true;
}

bool DemonButterscotchMixer_selfTest(DemonButterscotchMixerStats *stats) {
    if (stats == NULL) return false;
    memset(stats, 0, sizeof(*stats));
    stats->capacity = DEMON_BUTTERSCOTCH_VOICE_MAX;
    const int16_t clip[4] = {12000, -12000, 24000, -24000};
    int16_t output[12];
    DemonButterscotchMixer mixer;
    DemonButterscotchMixer_init(&mixer);
    const uint32_t looping = DemonButterscotchMixer_play(&mixer, clip, 2u,
        true, 256u, 0);
    const uint32_t panned = DemonButterscotchMixer_play(&mixer, clip, 2u,
        false, 128u, 256);
    if (looping == 0u || panned == 0u) return false;
    stats->voicesPlayed = 2u;
    if (!DemonButterscotchMixer_pause(&mixer, looping, true) ||
        !DemonButterscotchMixer_render(&mixer, output, 2u, stats) ||
        output[0] != 0 || output[1] != -6000 || stats->pausedFrames != 2u ||
        !DemonButterscotchMixer_pause(&mixer, looping, false) ||
        !DemonButterscotchMixer_render(&mixer, output, 6u, stats) ||
        stats->loopWraps != 2u || !DemonButterscotchMixer_stop(&mixer,
        looping) || DemonButterscotchMixer_stop(&mixer, panned)) return false;
    return true;
}

bool DemonButterscotchAudio_open(DemonButterscotchAudio *audio) {
    if (audio == NULL) return false;
    memset(audio, 0, sizeof(*audio));
    audio->handle = demon_service_open(AUDIO_SERVICE);
    return audio->handle != INVALID_HANDLE;
}

bool DemonButterscotchAudio_submit(DemonButterscotchAudio *audio,
                                   const int16_t *stereoSamples,
                                   uint32_t frames) {
    if (audio == NULL || stereoSamples == NULL || frames == 0u ||
        audio->handle == INVALID_HANDLE) return false;
    const uint64_t submitted = demon_audio_submit(audio->handle,
                                                   stereoSamples, frames);
    if (submitted != frames) return false;
    audio->framesSubmitted += submitted;
    ++audio->buffersSubmitted;
    return true;
}

bool DemonButterscotchAudio_selfTest(DemonButterscotchAudio *audio) {
    static int16_t samples[SELFTEST_FRAMES * 2u];
    uint32_t phase = 0u;
    for (uint32_t frame = 0u; frame < SELFTEST_FRAMES; ++frame) {
        int32_t amplitude = 1200;
        if (frame < 32u) amplitude = amplitude * (int32_t)frame / 32;
        if (SELFTEST_FRAMES - frame < 32u)
            amplitude = amplitude * (int32_t)(SELFTEST_FRAMES - frame) / 32;
        const int16_t sample = phase < 22050u ? (int16_t)amplitude :
                                               (int16_t)-amplitude;
        samples[frame * 2u] = sample;
        samples[frame * 2u + 1u] = sample;
        phase = (phase + 440u) % 44100u;
    }
    return DemonButterscotchAudio_submit(audio, samples, SELFTEST_FRAMES);
}

void DemonButterscotchAudio_close(DemonButterscotchAudio *audio) {
    if (audio == NULL) return;
    if (audio->handle != INVALID_HANDLE) demon_handle_close(audio->handle);
    audio->handle = INVALID_HANDLE;
}
