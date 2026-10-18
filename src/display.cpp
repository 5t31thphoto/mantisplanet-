#include "display.h"
#include "render.h"
#include <M5Unified.h>
#include <string.h>

namespace mantis {

static constexpr int DW = FB_W * 2, DH = FB_H * 2;

static M5Canvas s_fbA(&M5.Display), s_fbB(&M5.Display);
static M5Canvas* s_fb[2] = {&s_fbA, &s_fbB};
static int s_cur = 0;
static bool s_double = false;
static TaskHandle_t s_dispTask = nullptr;
static SemaphoreHandle_t s_dispIdle = nullptr;
static volatile int s_pushIdx = 0;

static void displayTask(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        s_fb[s_pushIdx]->pushSprite(&M5.Display, 0, 0);
        xSemaphoreGive(s_dispIdle);
    }
}

void displayInit() {
    for (int i = 0; i < 2; i++) { s_fb[i]->setColorDepth(16); s_fb[i]->setPsram(true); }
    s_fb[0]->createSprite(DW, DH);
    s_double = s_fb[1]->createSprite(DW, DH) != nullptr;
    if (s_double) {
        s_dispIdle = xSemaphoreCreateBinary();
        xSemaphoreGive(s_dispIdle);
        xTaskCreatePinnedToCore(displayTask, "lcd", 4096, nullptr, 2, &s_dispTask, 0);
    }
}

// 2x nearest upscale; the sprite stores pixels as byte-swapped RGB565, same as the palette.
static void expand(M5Canvas* c) {
    uint16_t* dst = (uint16_t*)c->getBuffer();
    if (!dst) return;
    const uint8_t* fb = frameBuffer();
    const uint16_t* pal = framePalette565();
    const int bob = frameBob();
    int prevSrc = -1;
    for (int y = 0; y < DH; y++) {
        int src = (y + bob) >> 1;
        if (src > FB_H - 1) src = FB_H - 1;
        uint16_t* d = dst + y * DW;
        if (src == prevSrc) { memcpy(d, d - DW, DW * sizeof(uint16_t)); continue; }
        const uint8_t* s = fb + src * FB_W;
        for (int x = 0; x < FB_W; x++) {
            uint16_t v = pal[s[x]];
            d[2 * x] = v;
            d[2 * x + 1] = v;
        }
        prevSrc = src;
    }
}

void displayPresent() {
    if (!s_double) {
        expand(s_fb[0]);
        s_fb[0]->pushSprite(&M5.Display, 0, 0);
        return;
    }
    // wait for the LCD to finish the previous frame
    while (xSemaphoreTake(s_dispIdle, 0) != pdTRUE) vTaskDelay(1);
    expand(s_fb[s_cur]);
    s_pushIdx = s_cur;
    xTaskNotifyGive(s_dispTask);
    s_cur ^= 1;
}

}
