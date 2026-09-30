#include "telemetry.h"
#include <windows.h>
#include <stdio.h>
#include <share.h>
#include <string.h>

namespace BLVR {

static INIT_ONCE g_TelemetryOnce = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION g_TelemetryLock;
static FILE* g_TelemetryFile = nullptr;
static bool g_TelemetryEnabled = false;
static unsigned g_TelemetryLinesSinceFlush = 0;

static BOOL CALLBACK InitializeTelemetryOnce(PINIT_ONCE, PVOID, PVOID*) {
    InitializeCriticalSection(&g_TelemetryLock);

    char enabled[16] = {};
    GetEnvironmentVariableA("BLVR_TELEMETRY", enabled, sizeof(enabled));
    g_TelemetryEnabled = enabled[0] == '1' || _stricmp(enabled, "true") == 0;
    if (!g_TelemetryEnabled) return TRUE;

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
            "{\"kind\":\"telemetry_start\",\"pid\":%lu}\n",
            static_cast<unsigned long>(GetCurrentProcessId()));
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
    if (g_TelemetryFile) {
        // kind and fields are produced internally from fixed format strings;
        // keep the line a single JSON object for streaming parsers.
        fprintf(g_TelemetryFile,
                "{\"kind\":\"%s\",\"tick_ms\":%llu,\"qpc\":%lld,%s}\n",
                kind,
                static_cast<unsigned long long>(tick),
                static_cast<long long>(qpc.QuadPart),
                fields);
        if (++g_TelemetryLinesSinceFlush >= 30u) {
            fflush(g_TelemetryFile);
            g_TelemetryLinesSinceFlush = 0;
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
