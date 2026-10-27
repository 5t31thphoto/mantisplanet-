#include "creatures.h"
#include "storage.h"
#include "mem.h"
#include "bestiary_gen.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

namespace mantis {

// ===================== built-in placeholder art (mid tier: 24 x 22, facing right) =====================
// b coat, l light coat, d dark, k black (eye, nose, hooves), w white, a antler (bone)
static const char* const P_STAND[22] = {
    "........................", "........................", ".................b...b..", "..................b.b...",
    "..................bll...", ".................bblll..", ".................bbbbbk.", "................dbbbl...",
    "...............dbbl.....", "..............dbbl......", ".............dbbl.......", ".dllllllllllbbbbl.......",
    ".dblllllllbbbbbbl.......", ".dbbbbbbbbbbbbbbl.......", ".dbbbbbbbbbbbbbd........", "..dddbbbbbbbbbdd........",
    "...dd.dd...dd.dd........", "...db.db...db.db........", "...db..b...db..b........", "...dd..d...dd..d........",
    "...d...d...d...d........", "...k...k...k...k........"};
static const char* const P_WALK2[22] = {
    "........................", "........................", ".................b...b..", "..................b.b...",
    "..................bll...", ".................bblll..", ".................bbbbbk.", "................dbbbl...",
    "...............dbbl.....", "..............dbbl......", ".............dbbl.......", ".dllllllllllbbbbl.......",
    ".dblllllllbbbbbbl.......", ".dbbbbbbbbbbbbbbl.......", ".dbbbbbbbbbbbbbd........", "..dddbbbbbbbbbdd........",
    "...dd.dd...dd.dd........", "...db.db...db.db........", "..db...b..db....b.......", "..d....d..d.....d.......",
    ".d.....d.d.......d......", ".k.....k.k.......k......"};
static const char* const P_GRAZE[22] = {
    "........................", "........................", "........................", "........................",
    "........................", "........................", "........................", "........................",
    "........................", "........................", "........................", ".dllllllllllbbbbl.......",
    ".dblllllllbbbbbbbb......", ".dbbbbbbbbbbbbbbbbb.....", ".dbbbbbbbbbbbbbd.dbb....", "..dddbbbbbbbbbdd..dbb...",
    "...dd.dd...dd.dd..b.bb..", "...db.db...db.db...bbll.", "...db..b...db..b...bblll", "...dd..d...dd..d....bbbk",
    "...d...d...d...d........", "...k...k...k...k........"};
static const char* const P_SNIFF[22] = {
    "........................", "........................", "........................", "........................",
    "........................", "........................", "........................", "...................b.b..",
    "....................bb..", "................dbbbbll.", "..............dbbbbbblll", ".dllllllllllbbbbbbbbbllk",
    ".dblllllllbbbbbbl.......", ".dbbbbbbbbbbbbbbl.......", ".dbbbbbbbbbbbbbd........", "..dddbbbbbbbbbdd........",
    "...dd.dd...dd.dd........", "...db.db...db.db........", "...db..b...db..b........", "...dd..d...dd..d........",
    "...d...d...d...d........", "...k...k...k...k........"};
static const char* const P_STOMP[22] = {
    "........................", "........................", "........................", ".................b...b..",
    "..................b.b...", "..................bll...", ".................bblll..", ".................bbbbbk.",
    "...............ddbbbl...", "..............dbbl......", ".............dbbl.......", ".dllllllllllbbbbl.......",
    ".dblllllllbbbbbbl.......", ".dbbbbbbbbbbbbbbl.......", ".dbbbbbbbbbbbbbd........", "..dddbbbbbbbbbdd........",
    "...dd.dd...dd.dd........", "...db.db...db.db.k......", "...db..b...db..b........", "...dd..d...dd............",
    "...d...d...d.............", "...k...k...k............."};
static const char* const P_AWAY[22] = {
    "........................", "........................", "........b......b........", ".........b....b.........",
    "..........bbbb..........", "..........bbbb..........", "...........bb...........", "..........bbbb..........",
    ".........bbbbbb.........", "........bbbbbbbb........", "........bbbbbbbb........", ".......dbbbbbbbbd.......",
    ".......dbbbbbbbbd.......", ".......dbbbwwbbbd.......", "........dbwwwwbd........", "........ddbbbbdd........",
    "........dd....dd........", "........db....bd........", "........db....bd........", "........dd....dd........",
    "........d......d........", "........k......k........"};
// front three-quarter view, walking toward the viewer (two frames)
static const char* const P_TOWARD[22] = {
    "........................", "........................", "...........b...b........", "............b.b.........",
    "............bbb.........", "...........bbbbb........", "...........kblbk........", "............lll.........",
    "............lkl.........", "............dbd.........", "...........dbbbd........", "..........dbwwbbbbbd....",
    "..........dbbbbbbbbbbd..", "..........dbbbbbbbbbbd..", "...........dbbbbbbbbd...", "...........db.db..db.db.",
    "...........db.db..db.db.", "...........db.db..db.db.", "...........dd.dd..dd.dd.", "...........d..d...d..d..",
    "...........d..d...d..d..", "...........k..k...k..k.."};
static const char* const P_TOWARD2[22] = {
    "........................", "........................", "...........b...b........", "............b.b.........",
    "............bbb.........", "...........bbbbb........", "...........kblbk........", "............lll.........",
    "............lkl.........", "............dbd.........", "...........dbbbd........", "..........dbwwbbbbbd....",
    "..........dbbbbbbbbbbd..", "..........dbbbbbbbbbbd..", "...........dbbbbbbbbd...", "...........db.db..db.db.",
    "...........db.db..db.db.", "...........db.db..db.db.", "...........dd.dd..dd.dd.", "...........d..d...d..d..",
    "...........k..d...d..k..", "..............k...k....."};
// the far stand (12 x 11); the other far poses are the mid ones at half size
static const char* const P_FAR[11] = {
    "............", ".........b.b", ".........bb.", ".........bbk", ".......bbb..", ".bbbbbbbbb..", ".bbbbbbbbb..",
    ".lbbbbbbbl..", "..b.b...b.b.", "..b.b...b.b.", "..d.d...d.d."};
// parts: antlers (side, from behind) and tails; each with its attach point
struct PartArt { const char* const* rows; int n; int ax, ay; };
static const char* const A_SIDE[4] = {"a..a..", "a.aa..", ".aa.a.", "aaa..."};            // attach: bottom, x 1
static const char* const A_FAR[2] = {"d.d", ".d."};
static const char* const A_AWAY[4] = {"a......a", ".a....a.", "..a..a..", "..a..a.."};
static const char* const T_DOWN[2] = {"b", "b"};
static const char* const T_FLAG[4] = {"ww.", "www", "ww.", ".w."};
static const char* const T_FLICK[2] = {"b", "w"};
static const char* const TA_DOWN[3] = {"bb", "bb", "b."};
static const char* const TA_FLAG[5] = {"..ww..", ".wwww.", "wwwwww", ".wwww.", "..ww.."};
static const char* const TA_FLICK[3] = {"..b", "bw.", "w.."};
static const char* const TF_DOWN[1] = {"b"};
static const char* const TF_FLAG[2] = {"ww", "w."};
static const char* const TF_FLICK[1] = {"w"};
struct PoseArt { uint8_t anim; const char* const* rows; int hx, hy, tx, ty; };
static const PoseArt MID_POSES[] = {
    {AN_STAND, P_STAND, 19, 4, 1, 11}, {AN_WALK, P_STAND, 19, 4, 1, 11}, {AN_WALK, P_WALK2, 19, 4, 1, 11},
    {AN_BOUND, P_STAND, 19, 4, 1, 11}, {AN_GRAZE, P_GRAZE, 20, 16, 1, 11}, {AN_ALERT, P_STAND, 19, 4, 1, 11},
    {AN_STOMP, P_STOMP, 19, 5, 1, 11}, {AN_STOMP, P_STAND, 19, 4, 1, 11}, {AN_SNIFF, P_SNIFF, 20, 9, 1, 11},
    {AN_NURSE, P_STAND, 19, 4, 1, 11}, {AN_AWAY, P_AWAY, 12, 4, 11, 12},
    {AN_TOWARD, P_TOWARD, 13, 4, 19, 12}, {AN_TOWARD, P_TOWARD2, 13, 4, 19, 12}};

// ===================== storage =====================
struct Anim { uint8_t n = 0; Spr f[AN_MAXF]; };
struct DeerSets {
    Anim body[TIER_N][AN_N];                  // the neutral deer, posed
    Spr ant[TIER_N], antAway[TIER_N];         // antlers: side, from behind
    Spr tail[TIER_N][2][TAIL_N];              // tails: side / from behind, down / flag / flick
    Anim out[SEX_N][TIER_N][AN_N];            // doe, buck, fawn, caribou cow
    Spr fawnTail[TIER_N][2][TAIL_N];
};
static DeerSets* s_d = nullptr;
static const uint8_t TIER_H[TIER_N] = {11, 22, 44};
static const char* const ANIM_N[AN_N] = {"stand", "walk", "bound", "graze", "alert", "stomp", "sniff", "nurse", "away", "toward"};
static const char* const TIER_NM[TIER_N] = {"far", "mid", "near"};
static const char* const TAIL_NM[TAIL_N] = {"down", "flag", "flick"};
static int s_loaded = 0;

static inline bool creatureIdx(uint8_t p) { return p == 0 || (p >= 144 && p < 160) || (p >= 208 && p < 224); }
static Spr alloc(int w, int h) { Spr s; s.w = (uint8_t)w; s.h = (uint8_t)h; s.px = (uint8_t*)memSlow((size_t)w * h); return s; }
static Spr copySpr(const Spr& a) { Spr s = alloc(a.w, a.h); if (s.px && a.px) memcpy(s.px, a.px, (size_t)a.w * a.h); s.ax = a.ax; s.ay = a.ay; s.tx = a.tx; s.ty = a.ty; s.k = a.k; return s; }
static Spr fromRows(const char* const* rows, int n, uint8_t (*colour)(char)) {
    int w = (int)strlen(rows[0]);
    Spr s = alloc(w, n);
    if (!s.px) return s;
    for (int y = 0; y < n; y++) for (int x = 0; x < w; x++) { char c = rows[y][x]; s.px[y * w + x] = c == '.' ? 0 : colour(c); }
    return s;
}
static Spr scaled(const Spr& a, float f) {                            // nearest-neighbour, anchors scaled too
    int w = (int)lroundf(a.w * f), h = (int)lroundf(a.h * f);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    Spr s = alloc(w, h);
    if (!s.px || !a.px) return s;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        int sx = (int)((x + 0.5f) * a.w / w), sy = (int)((y + 0.5f) * a.h / h);
        s.px[y * w + x] = a.px[sy * a.w + sx];
    }
    s.ax = (int8_t)lroundf(a.ax * f); s.ay = (int8_t)lroundf(a.ay * f); s.tx = (int8_t)lroundf(a.tx * f); s.ty = (int8_t)lroundf(a.ty * f); s.k = a.k;
    return s;
}

// ===================== the parts transforms =====================
static void bodyCols(const Spr& s, int* top, bool* body) {
    for (int x = 0; x < s.w; x++) {
        top[x] = s.h;
        for (int y = 0; y < s.h; y++) if (s.px[y * s.w + x]) { top[x] = y; break; }
        body[x] = top[x] < s.h && top[x] >= (int)(s.h * 0.42f);
    }
}
static void raise(Spr& s, bool front, int k) {                       // the hips (rear third) or shoulders (front third), up to k px
    if (k <= 0 || !s.px || s.w > 128) return;
    int top[128]; bool body[128];
    bodyCols(s, top, body);
    int x0 = -1, x1 = -1;
    for (int x = 0; x < s.w; x++) if (body[x]) { if (x0 < 0) x0 = x; x1 = x; }
    if (x0 < 0) return;
    int span = (x1 - x0 + 1) / 3; if (span < 1) span = 1;
    int a = front ? x1 - span + 1 : x0, b = front ? x1 : x0 + span - 1;
    for (int x = a; x <= b; x++) {
        if (!body[x]) continue;
        float m = (float)(x - a) / (float)(b - a + 1);
        int kk = (int)lroundf(k * (1.f - fabsf(m - 0.5f) * 1.2f));
        uint8_t c = s.px[top[x] * s.w + x];
        for (int j = 1; j <= kk && top[x] - j >= 0; j++) if (!s.px[(top[x] - j) * s.w + x]) s.px[(top[x] - j) * s.w + x] = c;
    }
}
static void stamp(Spr& s, const Spr& part, int atx, int aty) {
    if (!part.px || !s.px) return;
    for (int y = 0; y < part.h; y++) for (int x = 0; x < part.w; x++) {
        uint8_t c = part.px[y * part.w + x];
        int X = atx - part.ax + x, Y = aty - part.ay + y;
        if (c && X >= 0 && X < s.w && Y >= 0 && Y < s.h) s.px[Y * s.w + X] = c;
    }
}
static Spr fawnOf(const Spr& doe) {
    Spr s = scaled(doe, 0.6f);
    if (s.px && s.h >= 8) {
        int top[128]; bool body[128];
        bodyCols(s, top, body);
        for (int x = 0; x < s.w; x++) {
            if (!body[x]) continue;
            for (int j = 1; j <= 2; j++) { int y = top[x] + j; if (y < s.h && s.px[y * s.w + x] && ((x * 3 + j * 2) % 5) == 0) s.px[y * s.w + x] = 208 + 5; }
        }
    }
    return s;
}
static Spr twice(const Spr& a) {
    Spr s = alloc(a.w * 2, a.h * 2);
    if (!s.px) return s;
    for (int y = 0; y < s.h; y++) for (int x = 0; x < s.w; x++) s.px[y * s.w + x] = a.px[(y / 2) * a.w + x / 2];
    s.ax = (int8_t)(a.ax * 2); s.ay = (int8_t)(a.ay * 2); s.tx = (int8_t)(a.tx * 2); s.ty = (int8_t)(a.ty * 2);
    return s;
}
// a part sized for this body (the built-in near is the mid art drawn at 2x)
static Spr partFor(const Spr& part, const Spr& body) {
    if (!part.px) return part;
    if (part.k == body.k) return part;
    if (part.k == 2 && body.k == 1) return twice(part);
    return Spr();
}
static void derive() {
    static const int RAISE[TIER_N] = {0, 1, 2};
    for (int t = 0; t < TIER_N; t++) {
        for (int a = 0; a < AN_N; a++) {
            const Anim& B = s_d->body[t][a];
            for (int sx = 0; sx < SEX_N; sx++) s_d->out[sx][t][a].n = B.n;
            for (int f = 0; f < B.n; f++) {
                const Spr& b = B.f[f];
                int r = b.k > 1 ? RAISE[t] / b.k : RAISE[t];
                bool away = a == AN_AWAY || a == AN_TOWARD;                   // front and rear views: V antlers, no hip/shoulder shaping
                Spr ant = partFor(away ? s_d->antAway[t] : s_d->ant[t], b);
                Spr doe = copySpr(b); if (!away) raise(doe, false, r);         // a doe: fuller hips
                Spr buck = copySpr(b); if (!away) raise(buck, true, r);        // a buck: heavier shoulders, antlers
                if (ant.px) stamp(buck, ant, b.ax, b.ay);
                Spr cow = copySpr(doe); if (ant.px) stamp(cow, ant, b.ax, b.ay);   // a caribou cow: a doe, with antlers
                s_d->out[SEX_DOE][t][a].f[f] = doe;
                s_d->out[SEX_BUCK][t][a].f[f] = buck;
                s_d->out[SEX_COW][t][a].f[f] = cow;
                s_d->out[SEX_FAWN][t][a].f[f] = fawnOf(doe);
            }
        }
        for (int v = 0; v < 2; v++) for (int st = 0; st < TAIL_N; st++) s_d->fawnTail[t][v][st] = s_d->tail[t][v][st].px ? scaled(s_d->tail[t][v][st], 0.6f) : Spr();
    }
}
static Spr part(const char* const* rows, int n, int ax, int ay, uint8_t (*colour)(char)) { Spr s = fromRows(rows, n, colour); s.ax = (int8_t)ax; s.ay = (int8_t)ay; return s; }

void creaturesBuiltIn(uint8_t (*colour)(char), uint8_t (*bone)(char)) {
    if (!s_d) s_d = new DeerSets();
    // mid poses, then far (half size) and near (the mid at 2x)
    for (const PoseArt& p : MID_POSES) {
        Spr s = fromRows(p.rows, 22, colour);
        s.ax = (int8_t)p.hx; s.ay = (int8_t)p.hy; s.tx = (int8_t)p.tx; s.ty = (int8_t)p.ty;
        Anim& A = s_d->body[TIER_MID][p.anim];
        if (A.n < AN_MAXF) A.f[A.n++] = s;
    }
    for (int a = 0; a < AN_N; a++) {
        const Anim& M = s_d->body[TIER_MID][a];
        Anim& F = s_d->body[TIER_FAR][a];
        Anim& N = s_d->body[TIER_NEAR][a];
        F.n = N.n = M.n;
        for (int f = 0; f < M.n; f++) { F.f[f] = scaled(M.f[f], 0.5f); N.f[f] = M.f[f]; N.f[f].k = 2; }
    }
    Spr farStand = fromRows(P_FAR, 11, colour); farStand.ax = 10; farStand.ay = 2; farStand.tx = 1; farStand.ty = 5;
    s_d->body[TIER_FAR][AN_STAND].f[0] = farStand;
    // parts
    s_d->ant[TIER_MID] = part(A_SIDE, 4, 1, 4, bone);
    s_d->ant[TIER_FAR] = part(A_FAR, 2, 1, 2, bone);
    s_d->ant[TIER_NEAR] = s_d->ant[TIER_MID]; s_d->ant[TIER_NEAR].k = 2;
    s_d->antAway[TIER_MID] = part(A_AWAY, 4, 4, 4, bone);
    s_d->antAway[TIER_FAR] = scaled(s_d->antAway[TIER_MID], 0.5f);
    s_d->antAway[TIER_NEAR] = s_d->antAway[TIER_MID]; s_d->antAway[TIER_NEAR].k = 2;
    s_d->tail[TIER_MID][0][TAIL_DOWN] = part(T_DOWN, 2, 1, 0, colour);
    s_d->tail[TIER_MID][0][TAIL_FLAG] = part(T_FLAG, 4, 1, 3, colour);
    s_d->tail[TIER_MID][0][TAIL_FLICK] = part(T_FLICK, 2, 1, 1, colour);
    s_d->tail[TIER_MID][1][TAIL_DOWN] = part(TA_DOWN, 3, 1, 0, colour);
    s_d->tail[TIER_MID][1][TAIL_FLAG] = part(TA_FLAG, 5, 3, 4, colour);
    s_d->tail[TIER_MID][1][TAIL_FLICK] = part(TA_FLICK, 3, 1, 2, colour);
    s_d->tail[TIER_FAR][0][TAIL_DOWN] = part(TF_DOWN, 1, 1, 0, colour);
    s_d->tail[TIER_FAR][0][TAIL_FLAG] = part(TF_FLAG, 2, 1, 1, colour);
    s_d->tail[TIER_FAR][0][TAIL_FLICK] = part(TF_FLICK, 1, 1, 0, colour);
    for (int st = 0; st < TAIL_N; st++) {
        s_d->tail[TIER_FAR][1][st] = scaled(s_d->tail[TIER_MID][1][st], 0.5f);
        for (int v = 0; v < 2; v++) { s_d->tail[TIER_NEAR][v][st] = s_d->tail[TIER_MID][v][st]; s_d->tail[TIER_NEAR][v][st].k = 2; }
    }
    derive();
}

// ===================== the card =====================
static bool parseMps(const uint8_t* d, size_t n, Anim& out, int anchors, bool single) {
    if (n < 6 || d[0] != 'M' || d[1] != 'P' || d[2] != 'S') return false;
    int w = d[3], h = d[4], fr = d[5];
    if (w < 1 || h < 1 || w > 96 || h > 96 || fr < 1 || fr > AN_MAXF || (single && fr != 1)) return false;
    size_t hdr = 6 + (size_t)fr * 2 * anchors, need = hdr + (size_t)w * h * fr;
    if (n != need) return false;
    const uint8_t* px = d + hdr;
    for (size_t i = 0; i < (size_t)w * h * fr; i++) if (!creatureIdx(px[i])) return false;
    Anim a; a.n = (uint8_t)fr;
    for (int f = 0; f < fr; f++) {
        Spr s = alloc(w, h);
        if (!s.px) return false;
        memcpy(s.px, px + (size_t)f * w * h, (size_t)w * h);
        const uint8_t* an = d + 6 + f * 2 * anchors;
        s.ax = (int8_t)an[0]; s.ay = (int8_t)an[1];
        if (anchors > 1) { s.tx = (int8_t)an[2]; s.ty = (int8_t)an[3]; }
        a.f[f] = s;
    }
    out = a;
    return true;
}
static uint8_t* s_buf = nullptr;
static const size_t BUF_CAP = 6 + AN_MAXF * 4 + 96 * 96 * AN_MAXF;
static bool loadPart(const char* path, Spr& into) {
    size_t n; Anim a;
    if (storageReadFile(path, s_buf, BUF_CAP, &n) && parseMps(s_buf, n, a, 1, true)) { into = a.f[0]; return true; }
    return false;
}
struct ModSet { Anim a[2][TIER_N]; };
static ModSet* s_mods[BESTIARY_N];

int creaturesLoadMods() {
    if (!storageReady() || !s_d) return 0;
    if (!s_buf) s_buf = (uint8_t*)memSlow(BUF_CAP);
    if (!s_buf) return 0;
    int taken = 0;
    char p[96];
    for (int i = 0; i < BESTIARY_N; i++) {                          // every other creature
        const BestiaryEntry& e = BESTIARY[i];
        for (int an = 0; an < 2; an++) {
            if (!e.anim[an][0]) continue;
            for (int t = 0; t < TIER_N; t++) {
                if (!e.tierH[t]) continue;
                snprintf(p, sizeof p, "/mp-sprites/%s/%s_%s.mps", e.id, e.anim[an], TIER_NM[t]);
                size_t n; Anim a;
                if (!storageReadFile(p, s_buf, BUF_CAP, &n) || !parseMps(s_buf, n, a, 1, false)) continue;
                if (!s_mods[i]) s_mods[i] = new ModSet();
                s_mods[i]->a[an][t] = a;
                taken++;
            }
        }
    }
    for (int t = 0; t < TIER_N; t++) {
        snprintf(p, sizeof p, "/mp-sprites/deer/antlers_%s.mps", TIER_NM[t]); taken += loadPart(p, s_d->ant[t]);
        snprintf(p, sizeof p, "/mp-sprites/deer/antlers_away_%s.mps", TIER_NM[t]); taken += loadPart(p, s_d->antAway[t]);
        for (int st = 0; st < TAIL_N; st++) {
            snprintf(p, sizeof p, "/mp-sprites/deer/tail_%s_%s.mps", TAIL_NM[st], TIER_NM[t]); taken += loadPart(p, s_d->tail[t][0][st]);
            snprintf(p, sizeof p, "/mp-sprites/deer/tail_away_%s_%s.mps", TAIL_NM[st], TIER_NM[t]); taken += loadPart(p, s_d->tail[t][1][st]);
        }
        for (int an = 0; an < AN_N; an++) {
            snprintf(p, sizeof p, "/mp-sprites/deer/body_%s_%s.mps", ANIM_N[an], TIER_NM[t]);
            size_t n; Anim a;
            if (storageReadFile(p, s_buf, BUF_CAP, &n) && parseMps(s_buf, n, a, 2, false)) { s_d->body[t][an] = a; taken++; }
        }
    }
    if (taken) derive();
    s_loaded = taken;
    return taken;
}

int deerFrames(uint8_t sex, uint8_t tier, uint8_t anim) {
    if (!s_d || sex >= SEX_N || tier >= TIER_N || anim >= AN_N) return 0;
    int n = s_d->out[sex][tier][anim].n;
    return n ? n : s_d->out[sex][tier][AN_STAND].n;
}
const Spr* deerFrame(uint8_t sex, uint8_t tier, uint8_t anim, int frame) {
    if (!s_d) return nullptr;
    if (sex >= SEX_N) sex = SEX_DOE;
    if (tier >= TIER_N) tier = TIER_MID;
    if (anim >= AN_N || !s_d->out[sex][tier][anim].n) anim = AN_STAND;
    const Anim& A = s_d->out[sex][tier][anim];
    if (!A.n) return nullptr;
    return &A.f[(frame % A.n + A.n) % A.n];
}
const Spr* deerTail(uint8_t sex, uint8_t tier, bool away, uint8_t state) {
    if (!s_d || tier >= TIER_N || state >= TAIL_N) return nullptr;
    const Spr& s = sex == SEX_FAWN ? s_d->fawnTail[tier][away][state] : s_d->tail[tier][away][state];
    return s.px ? &s : nullptr;
}
int creatureModFor(uint8_t kind, uint8_t variant, uint32_t seed) {
    for (int i = 0; i < BESTIARY_N; i++) {
        const BestiaryEntry& e = BESTIARY[i];
        if (e.kind != kind || e.variant != variant) continue;
        if (e.seedParity >= 0 && (int)(seed & 1u) != e.seedParity) continue;
        return s_mods[i] ? i : -1;
    }
    return -1;
}
bool creatureModFlock(int i) { return i >= 0 && i < BESTIARY_N && BESTIARY[i].flock; }
const Spr* creatureModSprite(int i, bool moving, float pxPerMetre, int frame) {
    if (i < 0 || i >= BESTIARY_N || !s_mods[i]) return nullptr;
    const BestiaryEntry& e = BESTIARY[i];
    int an = (moving && e.anim[1][0]) ? 1 : 0;
    float want = e.heightM * pxPerMetre;                             // its height on screen now
    const Anim* best = nullptr; float bestErr = 1e9f;
    for (int pass = 0; pass < 2 && !best; pass++) {
        int a = pass == 0 ? an : 0;                                  // the moving art, else the resting art
        for (int t = 0; t < TIER_N; t++) {
            const Anim& A = s_mods[i]->a[a][t];
            if (!A.n) continue;
            float err = fabsf(logf((A.f[0].h + 0.5f) / (want + 0.5f)));   // the nearest size, by ratio
            if (err < bestErr) { bestErr = err; best = &A; }
        }
    }
    if (!best) return nullptr;
    return &best->f[(frame % best->n + best->n) % best->n];
}
const Spr* deerBodyFrame(uint8_t tier, uint8_t anim, int frame, int* frames) {
    if (!s_d || tier >= TIER_N || anim >= AN_N) return nullptr;
    const Anim& A = s_d->body[tier][anim];
    if (frames) *frames = A.n;
    return A.n ? &A.f[(frame % A.n + A.n) % A.n] : nullptr;
}
const Spr* deerPart(uint8_t tier, int which) {
    if (!s_d || tier >= TIER_N) return nullptr;
    const Spr* s = which == 0 ? &s_d->ant[tier] : which == 1 ? &s_d->antAway[tier] : which < 5 ? &s_d->tail[tier][0][which - 2] : which < 8 ? &s_d->tail[tier][1][which - 5] : nullptr;
    return s && s->px ? s : nullptr;
}
uint8_t deerTierFor(float pxTall) {
    if (pxTall < TIER_H[TIER_FAR] * 1.4f) return TIER_FAR;
    if (pxTall < TIER_H[TIER_MID] * 1.6f) return TIER_MID;
    return TIER_NEAR;
}

}
