#pragma once
#include <windows.h>
#include <stdio.h>

namespace BLVR {

void LogInit();
void Log(const char* format, ...);
void LogFlush();

} // namespace BLVR
