#include "planet.h"
#include "render.h"
#include "journey.h"
#include "storage.h"
#include "mem.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

namespace mantis {

const char* const PLANET_BIOME_IDS[17] = {"meadow", "forest", "lakes", "highlands", "falls", "autumn", "marsh", "birch", "taiga",
                                          "garden", "canyon", "redwoods", "coast", "karst", "tundra", "rainforest", "grove"};
static const char* const SKY_F[9] = {"zenith", "horizon", "glow", "cloud_dark", "cloud_lit", "sun", "ambient", "key", "fog"};
static const char* const BASE_F[9] = {"rock", "rock2", "snow", "hills", "grass", "leaf", "bark", "water", "stone"};
static const char* const RAMP_F[5] = {"leaf", "grass", "stone", "bark", "rock"};

// ---------------- a small strict JSON reader: objects, arrays, numbers, strings, true/false/null ----------------
struct JNode { uint8_t type; double num; const char* key; int keyLen; int first, next; };   // type: 0 null 1 num 2 str 3 arr 4 obj 5 bool
struct JParse {
    const char* s; size_t n, i; JNode* nodes; int cap, count; bool bad;
    void ws() { while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++; }
    int add(uint8_t t) { if (count >= cap) { bad = true; return -1; } nodes[count] = JNode{t, 0, nullptr, 0, -1, -1}; return count++; }
    bool str(const char** out, int* len) {
        if (i >= n || s[i] != '"') return false;
        size_t a = ++i;
        while (i < n && s[i] != '"') { if (s[i] == '\\') i++; i++; }
        if (i >= n) return false;
        *out = s + a; *len = (int)(i - a); i++;
        return true;
    }
    int value(int depth) {
        if (depth > 12) { bad = true; return -1; }
        ws();
        if (i >= n) { bad = true; return -1; }
        char c = s[i];
        if (c == '{' || c == '[') {
            bool obj = c == '{';
            int me = add(obj ? 4 : 3); if (me < 0) return -1;
            i++; ws();
            int prev = -1;
            if (i < n && s[i] == (obj ? '}' : ']')) { i++; return me; }
            while (!bad) {
                const char* k = nullptr; int kl = 0;
                if (obj) { ws(); if (!str(&k, &kl)) { bad = true; return -1; } ws(); if (i >= n || s[i] != ':') { bad = true; return -1; } i++; }
                int v = value(depth + 1); if (v < 0) return -1;
                nodes[v].key = k; nodes[v].keyLen = kl;
                if (prev < 0) nodes[me].first = v; else nodes[prev].next = v;
                prev = v;
                ws();
                if (i < n && s[i] == ',') { i++; continue; }
                if (i < n && s[i] == (obj ? '}' : ']')) { i++; return me; }
                bad = true; return -1;
            }
            return -1;
        }
        if (c == '"') { int me = add(2); if (me < 0) return -1; const char* v; int l; if (!str(&v, &l)) { bad = true; return -1; } return me; }
        if (!strncmp(s + i, "true", 4) || !strncmp(s + i, "false", 5)) { int me = add(5); i += s[i] == 't' ? 4 : 5; return me; }
        if (!strncmp(s + i, "null", 4)) { int me = add(0); i += 4; return me; }
        char* end = nullptr;
        char tmp[40]; size_t k = 0;
        while (i + k < n && k < sizeof tmp - 1 && strchr("+-0123456789.eE", s[i + k])) { tmp[k] = s[i + k]; k++; }
        tmp[k] = 0;
        double v = strtod(tmp, &end);
        if (end == tmp) { bad = true; return -1; }
        i += (size_t)(end - tmp);
        int me = add(1); if (me < 0) return -1;
        nodes[me].num = v;
        return me;
    }
};
static const JNode* N(const JParse& P, int i) { return i >= 0 ? &P.nodes[i] : nullptr; }
static int child(const JParse& P, int obj, const char* key) {
    if (obj < 0 || P.nodes[obj].type != 4) return -1;
    for (int c = P.nodes[obj].first; c >= 0; c = P.nodes[c].next)
        if ((int)strlen(key) == P.nodes[c].keyLen && !strncmp(P.nodes[c].key, key, P.nodes[c].keyLen)) return c;
    return -1;
}
static int item(const JParse& P, int arr, int k) {
    if (arr < 0 || P.nodes[arr].type != 3) return -1;
    int c = P.nodes[arr].first;
    while (c >= 0 && k-- > 0) c = P.nodes[c].next;
    return c;
}
// [r, g, b] (0..255) into a colour slot
static int setRGB(const JParse& P, int arr, float* slot) {
    if (arr < 0 || !slot || P.nodes[arr].type != 3) return 0;
    float v[3];
    for (int k = 0; k < 3; k++) {
        const JNode* e = N(P, item(P, arr, k));
        if (!e || e->type != 1 || e->num < 0 || e->num > 255) return 0;
        v[k] = (float)e->num;
    }
    if (item(P, arr, 3) >= 0) return 0;
    slot[0] = v[0]; slot[1] = v[1]; slot[2] = v[2];
    return 1;
}

bool planetLoad() {
    if (!storageReady()) return false;
    const size_t CAP = 96 * 1024;
    uint8_t* buf = (uint8_t*)memSlow(CAP);
    if (!buf) return false;
    size_t n = 0;
    bool ok = storageReadFile("/mp-sprites/planet.json", buf, CAP, &n);
    int applied = 0;
    if (ok) {
        JParse P{(const char*)buf, n, 0, (JNode*)memSlow(sizeof(JNode) * 6000), 6000, 0, false};
        int root = P.nodes ? P.value(0) : -1;
        if (root >= 0 && !P.bad && P.nodes[root].type == 4) {
            int sky = child(P, root, "sky");
            for (int k = 0; k < renderSkyKeys(); k++) {
                int key = item(P, sky, k);
                for (int f = 0; f < 9; f++) applied += setRGB(P, child(P, key, SKY_F[f]), renderColourSlot("sky", k, SKY_F[f]));
            }
            int base = child(P, root, "base");
            for (int f = 0; f < 9; f++) applied += setRGB(P, child(P, base, BASE_F[f]), renderColourSlot("base", 0, BASE_F[f]));
            static const char* const GROUPS[3] = {"accent", "creature", "aurora"};
            static const int GN[3] = {16, 16, 7};
            for (int g = 0; g < 3; g++) { int a = child(P, root, GROUPS[g]); for (int k = 0; k < GN[g]; k++) applied += setRGB(P, item(P, a, k), renderColourSlot(GROUPS[g], k, "")); }
            int biomes = child(P, root, "biomes");
            for (int b = 0; b < 17; b++) {
                int bo = child(P, biomes, PLANET_BIOME_IDS[b]);
                for (int r = 0; r < 5; r++) { int ra = child(P, bo, RAMP_F[r]); for (int sh = 0; sh < 4; sh++) applied += setRGB(P, item(P, ra, sh), journeyRampSlot(b, RAMP_F[r], sh)); }
            }
            journeyColoursChanged();
        }
        free(P.nodes);
    }
    free(buf);
    return applied > 0;
}

#ifdef HOST
static void rgb(FILE* f, const float* c) { fprintf(f, "[%d, %d, %d]", (int)(c[0] + 0.5f), (int)(c[1] + 0.5f), (int)(c[2] + 0.5f)); }
bool planetWriteDefaults(const char* path) {
    FILE* f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "{\n \"name\": \"Earth\",\n \"about\": \"MantisPlanet planet colours (RGB 0-255). Put this file at /mp-sprites/planet.json on the SD card to replace them; missing keys keep Earth's.\",\n");
    fprintf(f, " \"sky\": [\n");
    for (int k = 0; k < renderSkyKeys(); k++) {
        fprintf(f, "  {\"time\": %.2f", renderSkyKeyTime(k));
        for (int c = 0; c < 9; c++) { fprintf(f, ", \"%s\": ", SKY_F[c]); rgb(f, renderColourSlot("sky", k, SKY_F[c])); }
        fprintf(f, "}%s\n", k + 1 < renderSkyKeys() ? "," : "");
    }
    fprintf(f, " ],\n \"base\": {");
    for (int c = 0; c < 9; c++) { fprintf(f, "%s\"%s\": ", c ? ", " : "", BASE_F[c]); rgb(f, renderColourSlot("base", 0, BASE_F[c])); }
    fprintf(f, "},\n");
    static const char* const GROUPS[3] = {"accent", "creature", "aurora"};
    static const int GN[3] = {16, 16, 7};
    for (int g = 0; g < 3; g++) {
        fprintf(f, " \"%s\": [", GROUPS[g]);
        for (int k = 0; k < GN[g]; k++) { if (k) fprintf(f, ", "); rgb(f, renderColourSlot(GROUPS[g], k, "")); }
        fprintf(f, "],\n");
    }
    fprintf(f, " \"biomes\": {\n");
    for (int b = 0; b < 17; b++) {
        fprintf(f, "  \"%s\": {", PLANET_BIOME_IDS[b]);
        for (int r = 0; r < 5; r++) {
            fprintf(f, "%s\"%s\": [", r ? ", " : "", RAMP_F[r]);
            for (int sh = 0; sh < 4; sh++) { if (sh) fprintf(f, ", "); rgb(f, journeyRampSlot(b, RAMP_F[r], sh)); }
            fprintf(f, "]");
        }
        fprintf(f, "}%s\n", b + 1 < 17 ? "," : "");
    }
    fprintf(f, " }\n}\n");
    fclose(f);
    return true;
}
#endif

}
