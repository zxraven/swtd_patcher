/*
 * Still Wakes the Deep: ultrawide + DLSS FG (borderless) patcher
 *
 * Patches (signature based, each must match exactly once):
 *   1. Camera aspect constraint: cutscene cameras no longer force 16:9 pillarbox
 *      (FMinimalViewInfo::CalculateProjectionMatrixGivenViewRectangle, je -> jmp).
 *   2. DLSS FG runtime gate: FG mode is no longer forced to Off when
 *      FullscreenMode != Fullscreen (cmove ebx,eax -> mov ebx,eax).
 *   3. DLSS FG menu gate: menu item is no longer disabled in non-fullscreen
 *      window modes (je -> jmp).
 *
 * Usage:
 *   swtd_patcher.exe                 auto-detect exe in current / patcher folder
 *   swtd_patcher.exe <path-to-exe>   patch the given file (drag & drop works)
 *   swtd_patcher.exe --restore [path] restore from .bak
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
typedef wchar_t pchar;
#define P(s) L##s
#define pfopen _wfopen
#define pstrlen wcslen
#define pstrcpy wcscpy
#define pstrcat wcscat
#define pstrcmp wcscmp
#define PFMT "%ls"
#else
typedef char pchar;
#define P(s) s
#define pfopen fopen
#define pstrlen strlen
#define pstrcpy strcpy
#define pstrcat strcat
#define pstrcmp strcmp
#define PFMT "%s"
#endif

long long _ftelli64_compat(FILE *f);

#define W -1 /* wildcard */

typedef struct { const int *b; size_t n; } sig_t;

static int match_at(const uint8_t *d, size_t len, size_t pos, const int *b, size_t n) {
    if (pos + n > len) return 0;
    for (size_t i = 0; i < n; i++)
        if (b[i] != W && d[pos + i] != (uint8_t)b[i]) return 0;
    return 1;
}

/* Count matches; store first position. */
static int find_sig(const uint8_t *d, size_t len, const int *b, size_t n, size_t *first) {
    int cnt = 0;
    if (b[0] == W) return -1;
    uint8_t f = (uint8_t)b[0];
    for (size_t i = 0; i + n <= len; i++) {
        const uint8_t *p = memchr(d + i, f, len - n + 1 - i);
        if (!p) break;
        i = (size_t)(p - d);
        if (match_at(d, len, i, b, n)) { if (!cnt) *first = i; cnt++; }
    }
    return cnt;
}

/* ---------- Patch 1: camera constrain je -> jmp ---------- */
static const int CAM_ORIG[] = {0xF6,0x47,0x60,0x01,0x45,0x0F,0x57,0xDB,0xF2,0x44,0x0F,0x10,0x0D,W,W,W,W,0x0F,0x84,W,W,W,W};
static const int CAM_DONE[] = {0xF6,0x47,0x60,0x01,0x45,0x0F,0x57,0xDB,0xF2,0x44,0x0F,0x10,0x0D,W,W,W,W,0xE9,W,W,W,W,0x90};

/* ---------- Patch 2: FG runtime gate cmove -> mov ---------- */
static const int RT_ORIG[] = {0x44,0x39,0xAF,W,W,0x00,0x00,0x41,0x8B,0xDD,0x40,0x0F,0xB6,0xC6,0x0F,0x44,0xD8};
static const int RT_DONE[] = {0x44,0x39,0xAF,W,W,0x00,0x00,0x41,0x8B,0xDD,0x40,0x0F,0xB6,0xC6,0x8B,0xD8,0x90};

/* ---------- Patch 3: FG menu gate (anchored on "cmp bl,3" = NVIDIA DLSS) ---------- */
static const int UI_ANCHOR[] = {0x80,0xFB,0x03,0x74,0x2D,0x48,0x8D,0x15};
static const int UI_ORIG[]   = {0x85,0xDB,0x74,0x3A,0x4C,0x8D,0x0D};
static const int UI_DONE[]   = {0x85,0xDB,0xEB,0x3A,0x4C,0x8D,0x0D};
#define UI_WINDOW 0x90

static void ui_scan(const uint8_t *d, size_t len, int *n_orig, int *n_done, size_t *pos) {
    *n_orig = *n_done = 0;
    for (size_t i = 0; i + sizeof UI_ANCHOR / sizeof(int) <= len; i++) {
        const uint8_t *p = memchr(d + i, 0x80, len - i);
        if (!p) break;
        i = (size_t)(p - d);
        if (!match_at(d, len, i, UI_ANCHOR, sizeof UI_ANCHOR / sizeof(int))) continue;
        for (size_t j = i + 8; j < i + UI_WINDOW && j + 7 <= len; j++) {
            if (match_at(d, len, j, UI_ORIG, 7)) { if (!*n_orig && !*n_done) *pos = j; (*n_orig)++; break; }
            if (match_at(d, len, j, UI_DONE, 7)) { if (!*n_orig && !*n_done) *pos = j; (*n_done)++; break; }
        }
    }
}

#define N(a) (sizeof(a) / sizeof(int))

enum { ST_APPLY, ST_ALREADY, ST_FAIL };

static int classify(int n_orig, int n_done) {
    if (n_orig == 1 && n_done == 0) return ST_APPLY;
    if (n_orig == 0 && n_done == 1) return ST_ALREADY;
    return ST_FAIL;
}

static uint8_t *read_file(const pchar *path, size_t *len) {
    FILE *f = pfopen(path, P("rb"));
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long long sz = _ftelli64_compat(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *len = (size_t)sz;
    return buf;
}

static int write_file(const pchar *path, const uint8_t *d, size_t len) {
    FILE *f = pfopen(path, P("wb"));
    if (!f) return 0;
    int ok = fwrite(d, 1, len, f) == len;
    ok &= fclose(f) == 0;
    return ok;
}

static int file_exists(const pchar *path) {
    FILE *f = pfopen(path, P("rb"));
    if (f) { fclose(f); return 1; }
    return 0;
}

static void pause_exit(int code) {
#ifdef _WIN32
    printf("\nНажми Enter для выхода...");
    getchar();
#endif
    exit(code);
}

static const char *st_name(int st) {
    return st == ST_APPLY ? "будет применён" : st == ST_ALREADY ? "уже применён" : "НЕ НАЙДЕН";
}

static int find_target(pchar *out) {
    static const pchar *cands[] = {
        P("StillWakesTheDeep.exe"),
#ifdef _WIN32
        P("Habitat\\Binaries\\Win64\\Habitat-Win64-Shipping.exe"),
        P("Habitat\\Binaries\\Win64\\StillWakesTheDeep.exe"),
#else
        P("Habitat/Binaries/Win64/Habitat-Win64-Shipping.exe"),
#endif
    };
    pchar dirs[2][1024];
    int nd = 0;
    dirs[nd++][0] = 0; /* current dir */
#ifdef _WIN32
    {
        wchar_t me[1024];
        DWORD n = GetModuleFileNameW(NULL, me, 1024);
        if (n && n < 1024) {
            wchar_t *s = wcsrchr(me, L'\\');
            if (s) { s[1] = 0; wcscpy(dirs[nd++], me); }
        }
    }
#endif
    for (int i = 0; i < nd; i++)
        for (size_t c = 0; c < sizeof cands / sizeof cands[0]; c++) {
            pchar path[2048];
            pstrcpy(path, dirs[i]);
            pstrcat(path, cands[c]);
            FILE *f = pfopen(path, P("rb"));
            if (!f) continue;
            fseek(f, 0, SEEK_END);
            long long sz = _ftelli64_compat(f);
            fclose(f);
            if (sz > 50LL * 1024 * 1024) { pstrcpy(out, path); return 1; }
        }
    return 0;
}

static int run(int argc, pchar **argv) {
    int restore = 0;
    pchar target[2048] = {0};
    for (int i = 1; i < argc; i++) {
        if (!pstrcmp(argv[i], P("--restore"))) restore = 1;
        else pstrcpy(target, argv[i]);
    }
    if (!target[0] && !find_target(target)) {
        printf("Не найден exe игры.\n"
               "Положи патчер в папку игры (рядом с StillWakesTheDeep.exe) или перетащи exe на патчер.\n");
        pause_exit(1);
    }
    pchar bak[2100];
    pstrcpy(bak, target);
    pstrcat(bak, P(".bak"));
    printf("Файл: " PFMT "\n", target);

    if (restore) {
        size_t bl;
        uint8_t *b = read_file(bak, &bl);
        if (!b) { printf("Резервная копия не найдена: " PFMT "\n", bak); pause_exit(1); }
        if (!write_file(target, b, bl)) { printf("Не удалось записать файл (игра запущена? нет прав?)\n"); pause_exit(1); }
        printf("Восстановлено из резервной копии.\n");
        free(b);
        pause_exit(0);
    }

    size_t len;
    uint8_t *d = read_file(target, &len);
    if (!d) { printf("Не удалось прочитать файл.\n"); pause_exit(1); }
    if (len < 2 || d[0] != 'M' || d[1] != 'Z') { printf("Это не exe-файл.\n"); pause_exit(1); }

    size_t p_cam = 0, p_cam2 = 0, p_rt = 0, p_rt2 = 0, p_ui = 0;
    int cam = classify(find_sig(d, len, CAM_ORIG, N(CAM_ORIG), &p_cam), find_sig(d, len, CAM_DONE, N(CAM_DONE), &p_cam2));
    int rt  = classify(find_sig(d, len, RT_ORIG, N(RT_ORIG), &p_rt), find_sig(d, len, RT_DONE, N(RT_DONE), &p_rt2));
    int uo, ud; ui_scan(d, len, &uo, &ud, &p_ui);
    int ui = classify(uo, ud);

    printf("\n[1] Полосы в катсценах (камера):        %s\n", st_name(cam));
    printf("[2] DLSS FG без Fullscreen (движок):     %s\n", st_name(rt));
    printf("[3] DLSS FG без Fullscreen (меню):       %s\n", st_name(ui));

    if (cam == ST_FAIL || rt == ST_FAIL || ui == ST_FAIL) {
        printf("\nЧасть сигнатур не найдена: вероятно, игра обновилась и код изменился.\n"
               "Файл НЕ изменён.\n");
        free(d);
        pause_exit(2);
    }
    if (cam == ST_ALREADY && rt == ST_ALREADY && ui == ST_ALREADY) {
        printf("\nВсё уже пропатчено, ничего не делаю.\n");
        free(d);
        pause_exit(0);
    }

    if (!file_exists(bak)) {
        if (!write_file(bak, d, len)) { printf("\nНе удалось создать резервную копию. Файл НЕ изменён.\n"); pause_exit(1); }
        printf("\nРезервная копия: " PFMT "\n", bak);
    } else {
        printf("\nРезервная копия уже есть, не перезаписываю: " PFMT "\n", bak);
    }

    if (cam == ST_APPLY) {
        size_t j = p_cam + 17; /* 0F 84 rel32 -> E9 rel32+1, 90 */
        int32_t rel; memcpy(&rel, d + j + 2, 4);
        rel += 1;
        d[j] = 0xE9; memcpy(d + j + 1, &rel, 4); d[j + 5] = 0x90;
    }
    if (rt == ST_APPLY) {
        size_t j = p_rt + 14;
        d[j] = 0x8B; d[j + 1] = 0xD8; d[j + 2] = 0x90;
    }
    if (ui == ST_APPLY) d[p_ui + 2] = 0xEB;

    if (!write_file(target, d, len)) {
        printf("Не удалось записать файл (игра запущена? нет прав?). Оригинал в .bak\n");
        free(d);
        pause_exit(1);
    }
    printf("Готово. Патч применён.\n"
           "В GameUserSettings.ini поставь FullscreenMode=1 и родное разрешение,\n"
           "затем включи DLSS Frame Generation в меню.\n"
           "Откат: swtd_patcher.exe --restore\n");
    free(d);
    pause_exit(0);
    return 0;
}

#ifdef _WIN32
long long _ftelli64_compat(FILE *f) { return _ftelli64(f); }
int wmain(int argc, wchar_t **argv) {
    SetConsoleOutputCP(CP_UTF8);
    printf("Still Wakes the Deep: ultrawide + DLSS FG patcher\n\n");
    return run(argc, argv);
}
#else
long long _ftelli64_compat(FILE *f) { return ftello(f); }
int main(int argc, char **argv) {
    printf("Still Wakes the Deep: ultrawide + DLSS FG patcher\n\n");
    return run(argc, argv);
}
#endif
