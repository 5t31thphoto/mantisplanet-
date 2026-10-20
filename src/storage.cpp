#include "storage.h"
#include "display.h"
#include "mgimage.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef HOST
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#else
#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>
#endif

namespace mantis {

static bool s_ok = false;
static int s_nextFish = 0, s_nextPlanet = 0;
static constexpr int MAXC = 300;
static CatchRec s_c[MAXC];
static int s_nc = 0;
int catchCount() { return s_nc; }
const CatchRec& catchAt(int i) { return s_c[i]; }
static int biomeFromName(const char* n) { for (int b = 0; b < 32; b++) if (strcmp(biomeName((uint8_t)b), n) == 0) return b; return 0; }
// idx,seed,name,family,len,kg,tier,biome name,water,when[,biome id]
static void parseLine(const char* line) {
    if (s_nc >= MAXC) return;
    char buf[256]; strncpy(buf, line, sizeof buf - 1); buf[sizeof buf - 1] = 0;
    char* f[12]; int n = 0; char* p = buf;
    while (n < 12) { f[n++] = p; char* c = strchr(p, ','); if (!c) break; *c = 0; p = c + 1; }
    if (n < 10) return;
    CatchRec r; r.idx = atoi(f[0]); r.seed = (uint32_t)strtoul(f[1], nullptr, 10); r.lengthCm = (float)atof(f[4]); r.tier = (uint8_t)atoi(f[6]);
    r.water = (uint8_t)atoi(f[8]); r.biome = (uint8_t)(n >= 11 ? atoi(f[10]) : biomeFromName(f[7]));
    s_c[s_nc++] = r;
}

const char* biomeName(uint8_t b) {
    static const char* N[] = {"Meadow", "Forest", "Lake Country", "Highlands", "Waterfall Country", "Autumn Wood", "Marsh", "Birch Grove", "Taiga",
                              "Japanese Garden", "Red Rock Canyon", "Ancient Redwoods", "Sunset Coast", "Bamboo And Karst", "Frozen Tundra", "Rainforest", "Moonlit Grove"};
    return b < sizeof(N) / sizeof(N[0]) ? N[b] : "Somewhere";
}

#ifdef HOST
static const char* ROOT = "sdcard";
static void mk(const char* p) { mkdir(p, 0755); }
static int scanMax(const char* dir, const char* prefix) {
    int best = 0; DIR* d = opendir(dir); if (!d) return 0;
    while (dirent* e = readdir(d)) { int n = 0; char fmt[32]; snprintf(fmt, sizeof fmt, "%s_%%d.mg", prefix); if (sscanf(e->d_name, fmt, &n) == 1 && n > best) best = n; }
    closedir(d); return best;
}
bool storageBegin() {
    char p[128];
    mk(ROOT); snprintf(p, sizeof p, "%s/images", ROOT); mk(p);
    snprintf(p, sizeof p, "%s/images/fish", ROOT); mk(p); s_nextFish = scanMax(p, "fish") + 1;
    snprintf(p, sizeof p, "%s/images/planet", ROOT); mk(p); s_nextPlanet = scanMax(p, "planet") + 1;
    s_nc = 0;
    snprintf(p, sizeof p, "%s/images/fish/catches.csv", ROOT);
    if (FILE* f = fopen(p, "rb")) { char line[256]; while (fgets(line, sizeof line, f)) parseLine(line); fclose(f); }
    s_ok = true; return true;
}
bool storageLoadTrophy(int idx, uint16_t* out) {
    char p[128]; snprintf(p, sizeof p, "%s/images/fish/fish_%04d.mg", ROOT, idx);
    FILE* f = fopen(p, "rb"); if (!f) return false;
    uint8_t h[8]; if (fread(h, 1, 8, f) != 8 || memcmp(h, "MG01", 4) != 0) { fclose(f); return false; }
    uint8_t row[MG_W * 2];
    for (int y = 0; y < MG_H; y++) { if (fread(row, 1, sizeof row, f) != sizeof row) break; for (int x = 0; x < MG_W; x++) { uint16_t v = (uint16_t)(row[x * 2] | (row[x * 2 + 1] << 8)); out[y * MG_W + x] = v ? v : 0x0821; } }
    fclose(f); return true;
}
void storageDate(char* out, int n) { time_t t = time(nullptr); struct tm* tm = localtime(&t); strftime(out, n, "%Y-%m-%d %H:%M", tm); }
static bool writeMG(const char* path) {
    FILE* f = fopen(path, "wb"); if (!f) return false;
    uint8_t h[8]; mgHeader(h); fwrite(h, 1, 8, f);
    uint16_t row[MG_W]; uint8_t out[MG_W * 2];
    for (int y = 0; y < MG_H; y++) { compositeRow(y, row); for (int x = 0; x < MG_W; x++) { out[x * 2] = (uint8_t)(row[x] & 0xFF); out[x * 2 + 1] = (uint8_t)(row[x] >> 8); } fwrite(out, 1, sizeof out, f); }
    fclose(f); return true;
}
static bool appendLine(const char* path, const char* line) { FILE* f = fopen(path, "ab"); if (!f) return false; fputs(line, f); fclose(f); return true; }
static void fullPath(char* out, int n, const char* rel) { snprintf(out, n, "%s%s", ROOT, rel); }
#else
static int scanMax(const char* dir, const char* prefix) {
    int best = 0; File d = SD.open(dir); if (!d) return 0;
    char fmt[32]; snprintf(fmt, sizeof fmt, "%s_%%d.mg", prefix);
    for (File e = d.openNextFile(); e; e = d.openNextFile()) { int n = 0; const char* nm = e.name(); const char* sl = strrchr(nm, '/'); if (sl) nm = sl + 1; if (sscanf(nm, fmt, &n) == 1 && n > best) best = n; e.close(); }
    d.close(); return best;
}
bool storageBegin() {
    displayHoldBus();
    SPI.begin(GPIO_NUM_18, GPIO_NUM_38, GPIO_NUM_23, GPIO_NUM_4);       // Core2: the SD card shares the LCD's bus
    s_ok = SD.begin(GPIO_NUM_4, SPI, 25000000);
    if (s_ok) {
        if (!SD.exists("/images")) SD.mkdir("/images");
        if (!SD.exists("/images/fish")) SD.mkdir("/images/fish");
        if (!SD.exists("/images/planet")) SD.mkdir("/images/planet");
        s_nextFish = scanMax("/images/fish", "fish") + 1;
        s_nextPlanet = scanMax("/images/planet", "planet") + 1;
        s_nc = 0;
        File f = SD.open("/images/fish/catches.csv");
        if (f) { char line[256]; int n = 0; while (f.available()) { int c = f.read(); if (c == '\n' || n == 255) { line[n] = 0; parseLine(line); n = 0; } else line[n++] = (char)c; } f.close(); }
    }
    displayReleaseBus();
    return s_ok;
}
bool storageLoadTrophy(int idx, uint16_t* out) {
    if (!s_ok) return false;
    char p[64]; snprintf(p, sizeof p, "/images/fish/fish_%04d.mg", idx);
    displayHoldBus();
    File f = SD.open(p);
    if (!f) { displayReleaseBus(); return false; }
    uint8_t h[8]; f.read(h, 8);
    static uint8_t row[MG_W * 2];
    for (int y = 0; y < MG_H; y++) { f.read(row, sizeof row); for (int x = 0; x < MG_W; x++) { uint16_t v = (uint16_t)(row[x * 2] | (row[x * 2 + 1] << 8)); out[y * MG_W + x] = v ? v : 0x0821; } }
    f.close();
    displayReleaseBus();
    return true;
}
void storageDate(char* out, int n) {
    auto dt = M5.Rtc.getDateTime();
    if (dt.date.year < 2020) { out[0] = 0; return; }
    snprintf(out, n, "%04d-%02d-%02d %02d:%02d", dt.date.year, dt.date.month, dt.date.date, dt.time.hours, dt.time.minutes);
}
static bool writeMG(const char* path) {
    displayHoldBus();
    File f = SD.open(path, FILE_WRITE);
    if (!f) { displayReleaseBus(); return false; }
    uint8_t h[8]; mgHeader(h); f.write(h, 8);
    static uint16_t row[MG_W]; static uint8_t out[MG_W * 2 * 8];       // write 8 rows (5 KB) at a time
    for (int y = 0; y < MG_H; y++) {
        compositeRow(y, row);
        uint8_t* o = out + (y & 7) * MG_W * 2;
        for (int x = 0; x < MG_W; x++) { o[x * 2] = (uint8_t)(row[x] & 0xFF); o[x * 2 + 1] = (uint8_t)(row[x] >> 8); }
        if ((y & 7) == 7) f.write(out, sizeof out);
    }
    f.close();
    displayReleaseBus();
    return true;
}
static bool appendLine(const char* path, const char* line) {
    displayHoldBus();
    File f = SD.open(path, FILE_APPEND);
    bool ok = f; if (f) { f.print(line); f.close(); }
    displayReleaseBus();
    return ok;
}
static void fullPath(char* out, int n, const char* rel) { snprintf(out, n, "%s", rel); }
#endif

bool storageReady() { return s_ok; }

bool storageScreenshot() {
    if (!s_ok) return false;
    char rel[64], p[96];
    snprintf(rel, sizeof rel, "/images/planet/planet_%04d.mg", s_nextPlanet++);
    fullPath(p, sizeof p, rel);
    return writeMG(p);
}

bool storageSaveTrophy(const FishSpec& f) {
    if (!s_ok) return false;
    char rel[64], p[96], when[24], line[200];
    int idx = s_nextFish++;
    snprintf(rel, sizeof rel, "/images/fish/fish_%04d.mg", idx);
    fullPath(p, sizeof p, rel);
    bool ok = writeMG(p);
    storageDate(when, sizeof when);
    snprintf(line, sizeof line, "%d,%u,%s,%s,%.1f,%.2f,%d,%s,%d,%s,%d\n", idx, (unsigned)f.seed, f.name, f.familyName, f.lengthCm, f.weightKg, f.tier, biomeName(f.biome), f.water, when, f.biome);
    parseLine(line);
    char logp[96]; fullPath(logp, sizeof logp, "/images/fish/catches.csv");
    appendLine(logp, line);
    return ok;
}

}
