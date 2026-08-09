#ifndef DEMONOS_BUTTERSCOTCH_AUDIO_H
#define DEMONOS_BUTTERSCOTCH_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t handle;
    uint64_t framesSubmitted;
    uint32_t buffersSubmitted;
} DemonButterscotchAudio;

typedef struct {
    uint32_t sourceRate;
    uint32_t sourceFrames;
    uint32_t outputFrames;
    uint32_t channels;
    uint32_t bitsPerSample;
} DemonButterscotchPcmStats;

#define DEMON_BUTTERSCOTCH_VOICE_MAX 8u
typedef struct {
    const int16_t *samples;
    uint32_t frameCount;
    uint32_t position;
    uint32_t id;
    uint16_t gainQ8;
    int16_t panQ8;
    bool active;
    bool paused;
    bool loop;
} DemonButterscotchVoice;
typedef struct {
    DemonButterscotchVoice voices[DEMON_BUTTERSCOTCH_VOICE_MAX];
    uint32_t nextId;
} DemonButterscotchMixer;
typedef struct {
    uint32_t capacity;
    uint32_t voicesPlayed;
    uint32_t loopWraps;
    uint32_t pausedFrames;
    uint32_t clippedSamples;
} DemonButterscotchMixerStats;

bool DemonButterscotchAudio_open(DemonButterscotchAudio *audio);
bool DemonButterscotchAudio_submit(DemonButterscotchAudio *audio,
                                   const int16_t *stereoSamples,
                                   uint32_t frames);
bool DemonButterscotchAudio_selfTest(DemonButterscotchAudio *audio);
bool DemonButterscotchAudio_decodePcmWav(const uint8_t *bytes,
                                         uint32_t byteCount,
                                         int16_t *stereoOutput,
                                         uint32_t outputCapacityFrames,
                                         uint32_t *outputFrames,
                                         DemonButterscotchPcmStats *stats);
bool DemonButterscotchAudio_mixStereo(int16_t *destination,
                                      uint32_t destinationFrames,
                                      const int16_t *source,
                                      uint32_t sourceFrames);
bool DemonButterscotchAudio_pcmSelfTest(DemonButterscotchPcmStats *stats);
void DemonButterscotchMixer_init(DemonButterscotchMixer *mixer);
uint32_t DemonButterscotchMixer_play(DemonButterscotchMixer *mixer,
                                     const int16_t *samples,
                                     uint32_t frames, bool loop,
                                     uint16_t gainQ8, int16_t panQ8);
bool DemonButterscotchMixer_stop(DemonButterscotchMixer *mixer,
                                 uint32_t voiceId);
bool DemonButterscotchMixer_pause(DemonButterscotchMixer *mixer,
                                  uint32_t voiceId, bool paused);
bool DemonButterscotchMixer_render(DemonButterscotchMixer *mixer,
                                   int16_t *output, uint32_t frames,
                                   DemonButterscotchMixerStats *stats);
bool DemonButterscotchMixer_selfTest(DemonButterscotchMixerStats *stats);
void DemonButterscotchAudio_close(DemonButterscotchAudio *audio);

#endif
