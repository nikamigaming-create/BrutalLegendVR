#pragma once

namespace BLVR {
using GameplayInputMapper = void(*)(float&, float&, float&, float&, bool);
void RetailInputBridgeSetGameplayMapper(GameplayInputMapper mapper);
bool InstallRetailInputBridge();
bool RetailInputBridgeActive();
}
