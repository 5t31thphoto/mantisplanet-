#include "mgimage.h"

namespace mantis {

void mgHeader(uint8_t out[8]) {
    out[0] = 'M'; out[1] = 'G'; out[2] = '0'; out[3] = '1';
    out[4] = (uint8_t)(MG_W & 0xFF); out[5] = (uint8_t)(MG_W >> 8);
    out[6] = (uint8_t)(MG_H & 0xFF); out[7] = (uint8_t)(MG_H >> 8);
}

void mgRowFromFrame(uint8_t out[MG_W * 2], int y, const uint8_t* fb160, const uint8_t* pal) {
    const uint8_t* src = fb160 + (y >> 1) * 160;
    for (int x = 0; x < MG_W; x++) {
        const uint8_t* c = pal + src[x >> 1] * 3;
        uint16_t p = mgPixel(c[0], c[1], c[2]);
        out[x * 2] = (uint8_t)(p & 0xFF);                // little-endian
        out[x * 2 + 1] = (uint8_t)(p >> 8);
    }
}

}
