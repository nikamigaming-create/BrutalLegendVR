#include "log.h"
#include <stdarg.h>
#include <string>

namespace BLVR {

static CRITICAL_SECTION g_LogLock;
static FILE* g_LogFile = nullptr;
static bool g_Initialized = false;

void LogInit() {
    if (g_Initialized) return;
    InitializeCriticalSection(&g_LogLock);
    
    char logPath[MAX_PATH];
    GetModuleFileNameA(NULL, logPath, MAX_PATH);
    char* lastSlash = strrchr(logPath, '\\');
    if (lastSlash) {
        strcpy_s(lastSlash + 1, MAX_PATH - (lastSlash + 1 - logPath), "blvr.log");
    } else {
        strcpy_s(logPath, "blvr.log");
    }

    g_LogFile = _fsopen(logPath, "w", _SH_DENYNO);
    g_Initialized = true;
    
    Log("=== Brutal Legend VR (BLVR) Log Started ===");
    Log("Log file initialized at: %s", logPath);
}

void Log(const char* format, ...) {
    if (!g_Initialized) LogInit();
    
    EnterCriticalSection(&g_LogLock);
    if (g_LogFile) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(g_LogFile, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        
        va_list args;
        va_start(args, format);
        vfprintf(g_LogFile, format, args);
        va_end(args);
        
        fprintf(g_LogFile, "\n");
        fflush(g_LogFile);
    }
    LeaveCriticalSection(&g_LogLock);
}

void LogFlush() {
    if (g_LogFile) {
        fflush(g_LogFile);
    }
}

} // namespace BLVR
