#ifndef DEMONOS_BUTTERSCOTCH_VIDEO_H
#define DEMONOS_BUTTERSCOTCH_VIDEO_H

#include <stdbool.h>
#include <stdint.h>

#include "image_decode.h"

typedef struct {
    uint64_t factory;
    uint64_t surface;
    void *display;
    uint32_t window;
    uint32_t width;
    uint32_t height;
    uint32_t windowWidth;
    uint32_t windowHeight;
    uint32_t frames;
    uint32_t events;
    uint32_t keys;
    uint32_t pointerEvents;
    uint32_t keyMask;
    int32_t pointerX;
    int32_t pointerY;
    uint32_t buttonMask;
    uint32_t *uploadPixels;
    uint64_t uploadPixelCapacity;
    uint64_t uploadedPixels;
    uint32_t uploadAllocations;
    bool focused;
    bool inputChanged;
    bool quit;
} DemonButterscotchVideo;

bool DemonButterscotchVideo_open(DemonButterscotchVideo *video,
                                 const DemonDecodedImage *image,
                                 bool createWindow, uint32_t windowWidth,
                                 uint32_t windowHeight);
void DemonButterscotchVideo_pump(DemonButterscotchVideo *video);
bool DemonButterscotchVideo_present(DemonButterscotchVideo *video,
                                    const DemonDecodedImage *image);
void DemonButterscotchVideo_close(DemonButterscotchVideo *video);

#endif
