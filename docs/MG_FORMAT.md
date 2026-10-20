# The .mg image format (locked)

MantisPlanet saves every image (trophy shots, screenshots) in this format and only this format.

| Property            | Value                                   |
|---------------------|-----------------------------------------|
| Extension           | `.mg`                                   |
| Total size          | 153,608 bytes                           |
| Header              | 8 bytes                                 |
| Magic               | ASCII `MG01`                            |
| Width               | 320, uint16 little-endian               |
| Height              | 240, uint16 little-endian               |
| Pixel data          | 153,600 bytes, raw RGB565               |
| Pixel order         | row-major, left to right, top to bottom |
| Pixel encoding      | 5 bits red, 6 green, 5 blue             |
| Pixel byte order    | little-endian per 16-bit pixel          |
| Compression / alpha / row padding | none                      |
| Pixel offset        | `8 + 2 * (y * 320 + x)`                 |

The first eight bytes are always:

    4D 47 30 31 40 01 F0 00

Every following pair of bytes is one pixel.

Encoder: `src/mgimage.h` (streams row by row: no full-frame buffer needed). Tested byte-for-byte by
`tools/host/mgtest.cpp`.
