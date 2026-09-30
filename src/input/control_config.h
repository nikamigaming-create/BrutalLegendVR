#pragma once
#include "control_bindings.h"
#include <filesystem>
#include <string>

namespace BLVR {
extern const char* const NativeActionNames[NativeActionCount];
extern const char* const TouchInputNames[20];
bool LoadControlBindings(const std::filesystem::path&,ControlBindings&,std::string& error);
std::filesystem::path ControlsFilePath();
}
