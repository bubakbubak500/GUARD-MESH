#if defined(GUARD_SIMULATOR)
#include "../UiPlatform.h"
#include "SimPlatform.h"
#include "../MeshRadioTransport.h"
#include <cstdlib>
namespace ui { namespace platform {
bool savePreferences() { return the_mesh.savePrefs(); }
void applyRadioPreferences() { the_mesh.applyRadioFromPrefs(); }
int maxTransmitPower() { return MAX_LORA_TX_POWER; }
RadioTransport& radioTransport() { static MeshRadioTransport<MyMesh> radio(the_mesh); return radio; }
void* allocate(size_t bytes, bool external) { return heap_caps_malloc(bytes, external ? MALLOC_CAP_SPIRAM : MALLOC_CAP_8BIT); }
void release(void* memory) { free(memory); }
uint32_t milliseconds() { return millis(); }
void yieldUiWork() { vTaskDelay(1); }
bool localTime(time_t timestamp, struct tm& result) { return localtime_r(&timestamp, &result) != nullptr; }
void applyLocalTimezone(const char *timezone) { setenv("TZ", timezone, 1); tzset(); }
} }
#endif
