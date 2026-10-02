#include "telemetry.h"
#include <windows.h>
#include <stdio.h>
#include <share.h>
#include <string.h>
#include <stdlib.h>

namespace BLVR {

static INIT_ONCE g_TelemetryOnce = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION g_TelemetryLock;
static FILE* g_TelemetryFile = nullptr;
static bool g_TelemetryEnabled = false;
static unsigned g_TelemetryLinesSinceFlush = 0;
static ULONGLONG g_LastTelemetryFlushTick = 0;
static DWORD g_ContinuousIntervalMs = 0;
static ULONGLONG g_LastContinuousTick[3] = {};
static const unsigned long g_MaxTelemetryBytes = 32u * 1024u * 1024u;
static unsigned long g_TelemetryByteLimit = g_MaxTelemetryBytes;
static bool g_TelemetryBudgetReached = false;

static BOOL CALLBACK InitializeTelemetryOnce(PINIT_ONCE, PVOID, PVOID*) {
    InitializeCriticalSection(&g_TelemetryLock);

    char enabled[16] = {};
    GetEnvironmentVariableA("BLVR_TELEMETRY", enabled, sizeof(enabled));
    g_TelemetryEnabled = enabled[0] == '1' || _stricmp(enabled, "true") == 0;
    if (!g_TelemetryEnabled) return TRUE;

    // Diagnostics may use a smaller budget, never exceed the runtime cap.
    char budget[24] = {};
    const DWORD budgetLength = GetEnvironmentVariableA(
        "BLVR_TELEMETRY_MAX_BYTES", budget, sizeof(budget));
    if (budgetLength && budgetLength < sizeof(budget)) {
        char* end = nullptr;
        const unsigned long parsed = strtoul(budget, &end, 10);
        if (end != budget && *end == '\0' && parsed >= 4096u && parsed <= g_MaxTelemetryBytes)
            g_TelemetryByteLimit = parsed;
    }

    // Optional sampling for polling diagnostics. Discrete capture/solo events
    // remain complete; ordinary enabled telemetry retains its full cadence.
    char interval[16] = {};
    const DWORD intervalLength = GetEnvironmentVariableA(
        "BLVR_TELEMETRY_CONTINUOUS_INTERVAL_MS", interval, sizeof(interval));
    if (intervalLength && intervalLength < sizeof(interval)) {
        char* end = nullptr;
        const unsigned long parsed = strtoul(interval, &end, 10);
        if (end != interval && *end == '\0' && parsed <= 1000)
            g_ContinuousIntervalMs = static_cast<DWORD>(parsed);
    }

    char path[MAX_PATH] = {};
    DWORD pathLength = GetEnvironmentVariableA(
        "BLVR_TELEMETRY_PATH", path, static_cast<DWORD>(sizeof(path)));
    if (pathLength == 0 || pathLength >= sizeof(path)) {
        strcpy_s(path, "D:\\code\\blvr\\artifacts\\blvr_telemetry.jsonl");
    }

    CreateDirectoryA("D:\\code\\blvr\\artifacts", nullptr);
    g_TelemetryFile = _fsopen(path, "w", _SH_DENYNO);
    if (!g_TelemetryFile) {
        g_TelemetryEnabled = false;
        return TRUE;
    }

    // Keep file I/O off the render thread's critical path.  The trace is
    // flushed in small batches so a crash still leaves a useful tail.
    setvbuf(g_TelemetryFile, nullptr, _IOFBF, 1u << 20u);
    // Keep the start record JSON-safe without adding a second escaping path;
    // the selected path is already known to the launcher and the event stream
    // itself only contains numeric/fixed internal fields.
    fprintf(g_TelemetryFile,
            "{\"kind\":\"telemetry_start\",\"pid\":%lu,\"continuous_interval_ms\":%lu}\n",
            static_cast<unsigned long>(GetCurrentProcessId()),
            static_cast<unsigned long>(g_ContinuousIntervalMs));
    fflush(g_TelemetryFile);
    return TRUE;
}

void Telemetry_Init() {
    InitOnceExecuteOnce(&g_TelemetryOnce, InitializeTelemetryOnce, nullptr, nullptr);
}

bool Telemetry_Enabled() {
    Telemetry_Init();
    return g_TelemetryEnabled && g_TelemetryFile != nullptr;
}

void Telemetry_Write(const char* kind, const char* fields) {
    if (!Telemetry_Enabled() || !kind || !fields) return;

    LARGE_INTEGER qpc{};
    QueryPerformanceCounter(&qpc);
    const ULONGLONG tick = GetTickCount64();

    EnterCriticalSection(&g_TelemetryLock);
    if (g_TelemetryFile && !g_TelemetryBudgetReached) {
        // Reserve the terminal record before accepting another complete row.
        // _ftelli64 includes bytes buffered by stdio; no per-frame disk query.
        const auto position = _ftelli64(g_TelemetryFile);
        const size_t upperBound = strlen(kind) + strlen(fields) + 96u;
        if (position < 0 || upperBound > g_TelemetryByteLimit - 256u ||
            static_cast<unsigned long long>(position) + upperBound > g_TelemetryByteLimit - 256u) {
            g_TelemetryBudgetReached = true;
            if (position >= 0) fprintf(g_TelemetryFile,
                "{\"kind\":\"telemetry_stop\",\"reason\":\"byte_limit\",\"limit_bytes\":%lu}\n",
                g_TelemetryByteLimit);
            fflush(g_TelemetryFile);
            LeaveCriticalSection(&g_TelemetryLock);
            return;
        }
        const int continuousKind = strcmp(kind, "camera") == 0 ? 0
            : (strcmp(kind, "xr_tick") == 0 ? 1 : (strcmp(kind, "present") == 0 ? 2 : -1));
        if (g_ContinuousIntervalMs && continuousKind >= 0) {
            ULONGLONG& previous = g_LastContinuousTick[continuousKind];
            if (previous && tick - previous < g_ContinuousIntervalMs) {
                LeaveCriticalSection(&g_TelemetryLock);
                return;
            }
            previous = tick;
        }
        // kind and fields are produced internally from fixed format strings;
        // keep the line a single JSON object for streaming parsers.
        fprintf(g_TelemetryFile,
                "{\"kind\":\"%s\",\"tick_ms\":%llu,\"qpc\":%lld,%s}\n",
                kind,
                static_cast<unsigned long long>(tick),
                static_cast<long long>(qpc.QuadPart),
                fields);
        // A sampled stream must remain useful to a live observer even when
        // thirty rows take several seconds to accumulate.
        if (++g_TelemetryLinesSinceFlush >= 30u || tick - g_LastTelemetryFlushTick >= 500u) {
            fflush(g_TelemetryFile);
            g_TelemetryLinesSinceFlush = 0;
            g_LastTelemetryFlushTick = tick;
        }
    }
    LeaveCriticalSection(&g_TelemetryLock);
}

void Telemetry_Flush() {
    if (!Telemetry_Enabled()) return;
    EnterCriticalSection(&g_TelemetryLock);
    if (g_TelemetryFile) fflush(g_TelemetryFile);
    g_TelemetryLinesSinceFlush = 0;
    LeaveCriticalSection(&g_TelemetryLock);
}

void Telemetry_Shutdown() {
    if (!g_TelemetryEnabled) return;
    EnterCriticalSection(&g_TelemetryLock);
    if (g_TelemetryFile) {
        fflush(g_TelemetryFile);
        fclose(g_TelemetryFile);
        g_TelemetryFile = nullptr;
    }
    LeaveCriticalSection(&g_TelemetryLock);
    DeleteCriticalSection(&g_TelemetryLock);
    g_TelemetryEnabled = false;
}

} // namespace BLVR
