#ifndef KERNEL_AC97_H
#define KERNEL_AC97_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AC97_SAMPLE_RATE 44100u
#define AC97_MAX_FRAMES 4096u

bool ac97_init(void);
bool ac97_available(void);
bool ac97_submit(const int16_t *stereo_samples, size_t frame_count);
uint64_t ac97_buffers_submitted(void);

#endif
