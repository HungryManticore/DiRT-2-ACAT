/*
 * D2ACAT.asi - DiRT 2: Any Car, Any Track
 *
 * Lets Free Race offer every vehicle class you have unlocked, on any
 * track and in any game mode, the way LAN lets you pick any class.
 *
 * How it works
 * ------------
 * The Free Race "vehicle class" list is filled by one function in
 * dirt2_game.exe (0x0051FCE0 in the 22 Jan 2010 build):
 *
 *     void __thiscall BuildFreeRaceClassList(this, int mode, int track,
 *                                            GameVector<int>* out);
 *
 * It switches on the selected game mode and pushes only the classes that
 * belong to it (internal class ids: 0 rally, 1 rallycross, 2 trailblazer,
 * 3 stock baja, 4 raid T1, 5 trophy trucks, 6 class 1 buggies,
 * 8 baja+raid mix, 9 trucks+buggies mix). Only the Free Race class list
 * calls it.
 *
 * The hook calls the original once for each "base" mode (rally,
 * rallycross, trailblazer, raid/landrush), collects the class ids it
 * returns, then calls it for the mode you actually picked and appends
 * the collected ids after that mode's own classes. Because every id comes
 * from the game's own function, its unlock checks still apply: classes
 * you have not unlocked stay hidden.
 *
 * The function is found by byte signature, so if your exe differs the
 * plugin does nothing and says so in the log.
 *
 * Built without a C runtime (clang + lld-link), so only kernel32 is used.
 */

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef int            BOOL;
typedef void*          HANDLE;

/* ---- kernel32 imports (declared by hand: no Windows SDK needed) ---- */
__declspec(dllimport) BOOL   __stdcall VirtualProtect(void*, u32, u32, u32*);
__declspec(dllimport) void*  __stdcall VirtualAlloc(void*, u32, u32, u32);
__declspec(dllimport) void*  __stdcall GetModuleHandleA(const char*);
__declspec(dllimport) u32    __stdcall GetModuleFileNameA(void*, char*, u32);
__declspec(dllimport) HANDLE __stdcall CreateFileA(const char*, u32, u32, void*, u32, u32, HANDLE);
__declspec(dllimport) BOOL   __stdcall WriteFile(HANDLE, const void*, u32, u32*, void*);
__declspec(dllimport) BOOL   __stdcall CloseHandle(HANDLE);
__declspec(dllimport) u32    __stdcall GetPrivateProfileIntA(const char*, const char*, int, const char*);
__declspec(dllimport) BOOL   __stdcall FlushInstructionCache(HANDLE, const void*, u32);
__declspec(dllimport) HANDLE __stdcall GetCurrentProcess(void);
__declspec(dllimport) BOOL   __stdcall DisableThreadLibraryCalls(void*);

#define PAGE_EXECUTE_READWRITE 0x40
#define MEM_COMMIT_RESERVE     0x3000
#define GENERIC_WRITE          0x40000000
#define FILE_SHARE_READ        0x1
#define CREATE_ALWAYS          2
#define OPEN_ALWAYS            4
#define FILE_ATTRIBUTE_NORMAL  0x80
#define INVALID_HANDLE         ((HANDLE)(long)-1)
#define DLL_PROCESS_ATTACH     1

/* ---- game types ---- */
typedef struct GameVector {
    void* allocator;   /* game allocator object; vtbl[4] frees */
    int*  begin;
    int*  end;
    int*  capacity;
} GameVector;

/* thiscall emulated with fastcall: ecx = this, edx unused */
typedef void (__fastcall *BuildClassListFn)(void* self, void* edx, int mode, int track, GameVector* out);
/* game helper at 0x00536F50: push_back if not already present; ret 8 */
typedef char (__stdcall *PushUniqueFn)(int classId, GameVector* vec);

static BuildClassListFn g_original;   /* trampoline */
static PushUniqueFn     g_pushUnique;
static int  g_enabled = 1;
static int  g_expandEmpty = 0;
static int  g_logCalls = 0;

/* ---- tiny helpers (no CRT) ---- */
static u32 str_len(const char* s) { u32 n = 0; while (s[n]) n++; return n; }

static void str_cpy(char* d, const char* s) { while ((*d++ = *s++)) {} }

static void copy_bytes(u8* d, const u8* s, u32 n) { for (u32 i = 0; i < n; i++) d[i] = s[i]; }

static char g_logPath[520];
static char g_iniPath[520];

static void log_write(const char* text)
{
    if (!g_logPath[0]) return;
    HANDLE h = CreateFileA(g_logPath, 0x4 /* FILE_APPEND_DATA */, FILE_SHARE_READ, 0,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE) return;
    u32 w; WriteFile(h, text, str_len(text), &w, 0);
    CloseHandle(h);
}

static char* fmt_hex(char* p, u32 v)
{
    static const char digits[] = "0123456789ABCDEF";
    *p++ = '0'; *p++ = 'x';
    for (int i = 7; i >= 0; i--) *p++ = digits[(v >> (i * 4)) & 0xF];
    *p = 0;
    return p;
}

static char* fmt_int(char* p, int v)
{
    char tmp[12]; int n = 0; u32 u;
    if (v < 0) { *p++ = '-'; u = (u32)(-v); } else u = (u32)v;
    do { tmp[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    while (n) *p++ = tmp[--n];
    *p = 0;
    return p;
}

static char* cat(char* p, const char* s) { while (*s) *p++ = *s++; *p = 0; return p; }

/* ---- the hook ---- */
static void __fastcall Hook_BuildClassList(void* self, void* edx, int mode, int track, GameVector* out)
{
    (void)edx;
    if (!g_enabled) { g_original(self, 0, mode, track, out); return; }

    /* Base modes whose lists together cover every class:
       0 rally, 1 rallycross, 2 trailblazer, 3 raid (same list as landrush). */
    static const int baseModes[] = { 0, 1, 2, 3 };
    int extra[32];
    int nExtra = 0;

    for (int i = 0; i < 4; i++) {
        int m = baseModes[i];
        if (m == mode) continue;
        if (m == 3 && mode == 4) continue;          /* landrush already gives the raid list */
        g_original(self, 0, m, track, out);
        for (int* p = out->begin; p && p < out->end; p++) {
            int dup = 0;
            for (int k = 0; k < nExtra; k++) if (extra[k] == *p) { dup = 1; break; }
            if (!dup && nExtra < 32) extra[nExtra++] = *p;
        }
    }

    /* The list for the mode actually selected comes first, so the
       game's usual default class stays the default. */
    g_original(self, 0, mode, track, out);

    int ownCount = (out->begin && out->end) ? (int)(out->end - out->begin) : 0;
    if (ownCount == 0 && !g_expandEmpty) {
        /* The game shows no class list for this mode/track (e.g. a mode
           that does not use one): leave it alone unless the ini says so. */
        return;
    }

    for (int k = 0; k < nExtra; k++) g_pushUnique(extra[k], out);

    if (g_logCalls) {
        char buf[256]; char* p = buf;
        p = cat(p, "BuildClassList mode="); p = fmt_int(p, mode);
        p = cat(p, " track="); p = fmt_int(p, track);
        p = cat(p, " classes:");
        for (int* q = out->begin; q && q < out->end; q++) { p = cat(p, " "); p = fmt_int(p, *q); }
        cat(p, "\r\n");
        log_write(buf);
    }
}

/* ---- signature scan ---- */
/* First 0x47 bytes of the function; 0xFFFF = wildcard (jump offsets). */
static const u16 kSig[] = {
    0x53,0x56,0x57,0x8B,0x7C,0x24,0x18,0x8B,0x47,0x04,0x33,0xDB,0x3B,0xC3,0x8B,0xF1,
    0x74,0x0E,0x8B,0x0F,0x3B,0xCB,0x74,0x08,0x8B,0x11,0x50,0x8B,0x42,0x10,0xFF,0xD0,
    0x8B,0x44,0x24,0x10,0x3B,0xC3,0x89,0x5F,0x04,0x89,0x5F,0x08,0x89,0x5F,0x0C,0x0F,
    0x84,0xFFFF,0xFFFF,0x00,0x00,0x83,0xF8,0x08,0x0F,0x84,0xFFFF,0xFFFF,0x00,0x00,0x83,
    0xF8,0x01,0x75,0xFFFF,0x57,0x50,0x8B,0xCE,0xE8
};
#define SIG_LEN   (sizeof(kSig) / sizeof(kSig[0]))
#define CALL_OFS  0x47      /* offset of "E8 rel32" -> push-unique helper */
#define STOLEN    7         /* push ebx; push esi; push edi; mov edi,[esp+18h] */

static u8* find_function(u8* base)
{
    u32 peOfs = *(u32*)(base + 0x3C);
    u8* nt = base + peOfs;
    u16 nSections = *(u16*)(nt + 6);
    u16 optSize   = *(u16*)(nt + 20);
    u8* sec = nt + 24 + optSize;
    for (u16 s = 0; s < nSections; s++, sec += 40) {
        u32 chars = *(u32*)(sec + 36);
        if (!(chars & 0x20000000)) continue;            /* IMAGE_SCN_MEM_EXECUTE */
        u8* start = base + *(u32*)(sec + 12);
        u32 size  = *(u32*)(sec + 8);
        if (size < SIG_LEN) continue;
        for (u32 i = 0; i + SIG_LEN <= size; i++) {
            u32 j = 0;
            while (j < SIG_LEN && (kSig[j] == 0xFFFF || start[i + j] == (u8)kSig[j])) j++;
            if (j == SIG_LEN) return start + i;
        }
    }
    return 0;
}

static int install(void)
{
    char buf[256]; char* p;
    u8* base = (u8*)GetModuleHandleA(0);
    u8* fn = find_function(base);
    if (!fn) {
        log_write("ERROR: Free Race class-list function not found. "
                  "This exe is a different version; nothing was patched.\r\n");
        return 0;
    }

    g_pushUnique = (PushUniqueFn)(fn + CALL_OFS + 5 + *(int*)(fn + CALL_OFS + 1));

    /* trampoline: stolen bytes + jmp back */
    u8* tramp = (u8*)VirtualAlloc(0, 32, MEM_COMMIT_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) { log_write("ERROR: VirtualAlloc failed.\r\n"); return 0; }
    copy_bytes(tramp, fn, STOLEN);
    tramp[STOLEN] = 0xE9;
    *(int*)(tramp + STOLEN + 1) = (int)((fn + STOLEN) - (tramp + STOLEN + 5));
    g_original = (BuildClassListFn)tramp;

    /* patch: jmp hook; nop; nop */
    u32 old;
    if (!VirtualProtect(fn, STOLEN, PAGE_EXECUTE_READWRITE, &old)) {
        log_write("ERROR: VirtualProtect failed.\r\n"); return 0;
    }
    fn[0] = 0xE9;
    *(int*)(fn + 1) = (int)((u8*)&Hook_BuildClassList - (fn + 5));
    fn[5] = 0x90; fn[6] = 0x90;
    VirtualProtect(fn, STOLEN, old, &old);
    FlushInstructionCache(GetCurrentProcess(), fn, STOLEN);

    p = buf;
    p = cat(p, "Hooked class-list function at "); p = fmt_hex(p, (u32)fn);
    p = cat(p, " (exe base "); p = fmt_hex(p, (u32)base);
    p = cat(p, "), push helper at "); p = fmt_hex(p, (u32)g_pushUnique);
    cat(p, "\r\n");
    log_write(buf);
    return 1;
}

static void setup_paths(void* hinst)
{
    char path[520];
    u32 n = GetModuleFileNameA(hinst, path, sizeof(path) - 8);
    if (!n || n >= sizeof(path) - 8) return;
    /* strip extension */
    int dot = -1;
    for (int i = (int)n - 1; i >= 0 && path[i] != '\\' && path[i] != '/'; i--)
        if (path[i] == '.') { dot = i; break; }
    if (dot < 0) dot = (int)n;
    path[dot] = 0;
    str_cpy(g_iniPath, path); str_cpy(g_iniPath + dot, ".ini");
    str_cpy(g_logPath, path); str_cpy(g_logPath + dot, ".log");
}

BOOL __stdcall DllMain(void* hinst, u32 reason, void* reserved)
{
    (void)reserved;
    if (reason != DLL_PROCESS_ATTACH) return 1;
    DisableThreadLibraryCalls(hinst);
    setup_paths(hinst);

    /* fresh log each run */
    HANDLE h = CreateFileA(g_logPath, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h != INVALID_HANDLE) CloseHandle(h);
    log_write("D2ACAT 1.0\r\n");

    g_enabled     = (int)GetPrivateProfileIntA("Settings", "Enabled", 1, g_iniPath);
    g_expandEmpty = (int)GetPrivateProfileIntA("Settings", "ExpandEmptyLists", 0, g_iniPath);
    g_logCalls    = (int)GetPrivateProfileIntA("Settings", "LogClassLists", 0, g_iniPath);

    if (!g_enabled) { log_write("Disabled in ini; nothing patched.\r\n"); return 1; }
    install();
    return 1;
}
