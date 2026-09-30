#pragma once

namespace BLVR {

// A low-overhead, machine-readable trace for correlating XR pose, camera,
// Present, and capture/mailbox timing.  It is disabled unless
// BLVR_TELEMETRY=1 (or true) is present in the process environment.
void Telemetry_Init();
void Telemetry_Shutdown();
bool Telemetry_Enabled();
void Telemetry_Write(const char* kind, const char* fields);
void Telemetry_Flush();

} // namespace BLVR
