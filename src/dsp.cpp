#include "dsp.h"
#include <stdlib.h>
#include <string.h>
#ifndef HOST
#include <esp_heap_caps.h>
#endif

namespace dsp {

float SINE[1025];

void init() {
    for (int i = 0; i <= 1024; i++) SINE[i] = sinf(TWO_PI_F * i / 1024.f);
}

static void* alloc(size_t bytes) {
#ifndef HOST
    void* p = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    void* p = malloc(bytes);
#endif
    if (p) memset(p, 0, bytes);
    return p;
}

bool Reverb::begin() {
    ok = true;
    for (int j = 0; j < 4; j++) { line[j] = (float*)alloc(len[j] * sizeof(float)); if (!line[j]) ok = false; }
    for (int j = 0; j < 2; j++) { ap[j] = (float*)alloc(apLen[j] * sizeof(float)); if (!ap[j]) ok = false; }
    return ok;
}

bool DubDelay::begin(int samples) {
    buf = (int16_t*)alloc(samples * sizeof(int16_t));
    size = buf ? samples : 0;
    return buf != nullptr;
}

}
