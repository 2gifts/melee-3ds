/* Platform layer (see slippi_plat.h): libctru on the 3DS, Win32 for the PC tools. */
#include "slippi_plat.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void (*log_sink)(const char *line);
static char data_dir[160];

void sp_set_log_sink(void (*sink)(const char *line)) { log_sink = sink; }

void sp_set_data_dir(const char *dir)
{
    snprintf(data_dir, sizeof(data_dir), "%s", dir ? dir : "");
}

char *sp_read_file(const char *path, size_t *length)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    if (n < 0 || n > 1024 * 1024) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = '\0';
    if (length) *length = got;
    return buf;
}

static uint32_t rng_state;
uint32_t sp_random(void)
{
    if (!rng_state) {
        uint64_t t = sp_time_us();
        rng_state = (uint32_t)(t ^ (t >> 32)) | 1u;
    }
    /* xorshift32 */
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

#ifdef __3DS__
/* ------------------------------------------------------------------ 3DS */
#include <3ds.h>
#include <malloc.h>
#include <sys/socket.h>
#include <arpa/inet.h>

extern void mp_native_log(const char *text);

static LightLock state_lock, log_lock;
static int locks_ready;
static Thread worker;
static Handle worker_handle_id;
static u32 worker_thread_id;
static u32 *soc_buffer;
static int soc_ready, ac_ready;

#define SOC_BUFFER_SIZE 0x100000

uint64_t sp_time_us(void)
{
    u64 tick = svcGetSystemTick();
    return (tick / SYSCLOCK_ARM11) * 1000000ull + (tick % SYSCLOCK_ARM11) * 1000000ull / SYSCLOCK_ARM11;
}

void sp_sleep_us(uint32_t us) { svcSleepThread((s64)us * 1000); }

void sp_lock_init(void)
{
    if (locks_ready) return;
    LightLock_Init(&state_lock);
    LightLock_Init(&log_lock);
    locks_ready = 1;
}
void sp_lock(void) { LightLock_Lock(&state_lock); }
void sp_unlock(void) { LightLock_Unlock(&state_lock); }

/* Lines from the network thread wait here: mp_log_append is only safe from
 * the engine thread in every build flavour. */
#define LOG_QUEUE 8192
static char log_queue[LOG_QUEUE];
static unsigned log_used, log_lost;

static void emit(const char *line)
{
    if (log_sink) { log_sink(line); return; }
    char text[320];
    snprintf(text, sizeof(text), "[slippi] %s\n", line);
    mp_native_log(text);
}

void sp_log_drain(void)
{
    if (!locks_ready || sp_on_thread()) return;
    char copy[LOG_QUEUE];
    unsigned n, lost;
    LightLock_Lock(&log_lock);
    n = log_used;
    lost = log_lost;
    memcpy(copy, log_queue, n);
    log_used = 0;
    log_lost = 0;
    LightLock_Unlock(&log_lock);
    for (unsigned i = 0; i < n;) {
        emit(copy + i);
        i += strlen(copy + i) + 1;
    }
    if (lost) {
        char text[64];
        snprintf(text, sizeof(text), "(%u network-thread log lines dropped)", lost);
        emit(text);
    }
}

/* Players post game.log in public bug reports: public IPv4 addresses (the
 * opponent's home connection) become "public-ip"; private LAN, loopback and
 * link-local addresses stay, as they tell a LAN match from an internet one. */
static int ip_octet(const char **s)
{
    int v = 0, n = 0;
    while (**s >= '0' && **s <= '9' && n < 3) { v = v * 10 + (**s - '0'); ++*s; ++n; }
    return n && v <= 255 ? v : -1;
}

static void mask_public_ips(char *line, size_t size)
{
    char out[256];
    size_t o = 0;
    const char *s = line;
    while (*s && o + 1 < sizeof(out)) {
        int boundary = s == line || !((s[-1] >= '0' && s[-1] <= '9') || s[-1] == '.');
        if (boundary && *s >= '0' && *s <= '9') {
            const char *e = s;
            int q[4], ok = 1;
            for (int i = 0; i < 4 && ok; i++) {
                q[i] = ip_octet(&e);
                if (q[i] < 0) ok = 0;
                else if (i < 3) { if (*e == '.') e++; else ok = 0; }
            }
            if (ok && !((*e >= '0' && *e <= '9') || (*e == '.' && e[1] >= '0' && e[1] <= '9'))) {
                int private_ = q[0] == 10 || q[0] == 127 || (q[0] == 192 && q[1] == 168) ||
                               (q[0] == 172 && q[1] >= 16 && q[1] <= 31) || (q[0] == 169 && q[1] == 254) ||
                               (q[0] == 0) || (q[0] == 1 && q[1] == 1 && q[2] == 1 && q[3] == 1);
                const char *text = private_ ? NULL : "public-ip";
                if (text) {
                    for (; *text && o + 1 < sizeof(out); text++) out[o++] = *text;
                } else {
                    for (const char *c = s; c < e && o + 1 < sizeof(out); c++) out[o++] = *c;
                }
                s = e;
                continue;
            }
        }
        out[o++] = *s++;
    }
    out[o] = 0;
    snprintf(line, size, "%s", out);
}

void sp_log(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    mask_public_ips(line, sizeof(line));
    if (!locks_ready || !sp_on_thread()) {
        sp_log_drain();
        emit(line);
        return;
    }
    size_t n = strlen(line) + 1;
    LightLock_Lock(&log_lock);
    if (log_used + n <= LOG_QUEUE) { memcpy(log_queue + log_used, line, n); log_used += n; }
    else ++log_lost;
    LightLock_Unlock(&log_lock);
}

static void (*thread_fn)(void *);
static void thread_entry(void *arg)
{
    svcGetThreadId(&worker_thread_id, CUR_THREAD_HANDLE);
    thread_fn(arg);
}

int sp_thread_start(void (*fn)(void *), void *arg, int core)
{
    if (worker) return -1;
    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    thread_fn = fn;
    worker_thread_id = 0xFFFFFFFFu;
    worker = threadCreate(thread_entry, arg, 32 * 1024, priority > 0x18 ? priority - 1 : priority, core, false);
    (void)worker_handle_id;
    return worker ? 0 : -1;
}

void sp_thread_join(void)
{
    if (!worker) return;
    threadJoin(worker, U64_MAX);
    threadFree(worker);
    worker = NULL;
    worker_thread_id = 0xFFFFFFFFu;
}

int sp_on_thread(void)
{
    if (!worker) return 0;
    u32 id = 0;
    svcGetThreadId(&id, CUR_THREAD_HANDLE);
    return id == worker_thread_id;
}

int sp_net_init(void)
{
    if (soc_ready) return 0;
    if (!soc_buffer) soc_buffer = (u32 *)memalign(0x1000, SOC_BUFFER_SIZE);
    if (!soc_buffer) { sp_log("socInit: cannot allocate %u bytes", SOC_BUFFER_SIZE); return -1; }
    Result rc = socInit(soc_buffer, SOC_BUFFER_SIZE);
    if (R_FAILED(rc)) {
        sp_log("socInit failed: 0x%08lx", (unsigned long)rc);
        free(soc_buffer);
        soc_buffer = NULL;
        return -1;
    }
    soc_ready = 1;
    ac_ready = R_SUCCEEDED(acInit());
    return 0;
}

void sp_net_exit(void)
{
    if (ac_ready) { acExit(); ac_ready = 0; }
    if (!soc_ready) return;
    socExit();
    soc_ready = 0;
    free(soc_buffer);
    soc_buffer = NULL;
}

uint32_t sp_lan_ip_fallback(void)
{
    if (!soc_ready) return 0;
    long id = gethostid();
    return (uint32_t)id; /* already network byte order (in_addr.s_addr) */
}

int sp_wifi_status(void)
{
    if (!ac_ready) return -1;
    u32 status = 0;
    if (R_FAILED(ACU_GetWifiStatus(&status))) return -1;
    return status != 0;
}

const char *sp_data_dir(void) { return data_dir[0] ? data_dir : "sdmc:/3ds/melee/slippi"; }
int sp_app_running(void) { return aptMainLoop(); }

#elif defined(_WIN32)
/* ---------------------------------------------------------------- Windows */
#include <winsock2.h>
#include <windows.h>

static CRITICAL_SECTION state_lock, log_lock;
static int locks_ready;
static HANDLE worker;
static DWORD worker_id;

uint64_t sp_time_us(void)
{
    static LARGE_INTEGER freq;
    LARGE_INTEGER now;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (uint64_t)(now.QuadPart / freq.QuadPart) * 1000000ull + (uint64_t)(now.QuadPart % freq.QuadPart) * 1000000ull / (uint64_t)freq.QuadPart;
}

void sp_sleep_us(uint32_t us)
{
    static int period;
    if (!period) { timeBeginPeriod(1); period = 1; }
    uint64_t end = sp_time_us() + us;
    if (us >= 2000) Sleep((us - 1500) / 1000);
    while (sp_time_us() < end) Sleep(0);
}

void sp_lock_init(void)
{
    if (locks_ready) return;
    InitializeCriticalSection(&state_lock);
    InitializeCriticalSection(&log_lock);
    locks_ready = 1;
}
void sp_lock(void) { EnterCriticalSection(&state_lock); }
void sp_unlock(void) { LeaveCriticalSection(&state_lock); }

void sp_log_drain(void) {}

void sp_log(const char *fmt, ...)
{
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (locks_ready) EnterCriticalSection(&log_lock);
    if (log_sink) log_sink(line);
    else {
        static uint64_t t0;
        uint64_t t = sp_time_us();
        if (!t0) t0 = t;
        fprintf(stdout, "[%8.3f] %s\n", (double)(t - t0) / 1e6, line);
        fflush(stdout);
    }
    if (locks_ready) LeaveCriticalSection(&log_lock);
}

static void (*thread_fn)(void *);
static DWORD WINAPI thread_entry(LPVOID arg)
{
    thread_fn(arg);
    return 0;
}

int sp_thread_start(void (*fn)(void *), void *arg, int core)
{
    (void)core;
    if (worker) return -1;
    thread_fn = fn;
    worker = CreateThread(NULL, 0, thread_entry, arg, 0, &worker_id);
    return worker ? 0 : -1;
}

void sp_thread_join(void)
{
    if (!worker) return;
    WaitForSingleObject(worker, INFINITE);
    CloseHandle(worker);
    worker = NULL;
    worker_id = 0;
}

int sp_on_thread(void) { return worker && GetCurrentThreadId() == worker_id; }

int sp_net_init(void) { return 0; } /* enet_initialize() runs WSAStartup */
void sp_net_exit(void) {}
uint32_t sp_lan_ip_fallback(void) { return 0; }
int sp_wifi_status(void) { return -1; }
const char *sp_data_dir(void) { return data_dir[0] ? data_dir : "."; }
int sp_app_running(void) { return 1; }

#else
#error "slippi_plat.c: unsupported platform"
#endif
