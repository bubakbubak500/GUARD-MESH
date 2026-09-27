// SPDX-License-Identifier: GPL-3.0-or-later
#include "../OtaCapability.h"
#if defined(ESP32)
#include <esp_ota_ops.h>
#include <esp_spi_flash.h>
namespace ui {
namespace platform {
bool hasOtaUpdateSlot() {
  const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
  const esp_partition_t *running = esp_ota_get_running_partition();
  // Launcher/AppFS or a legacy smaller spare cannot take this firmware image.
  if (!next || !running || next->size < running->size)
    return false;
#if !defined(HAS_TANMATSU)
  // Some legacy tables describe a different slot from the executing image.
  // Cross-check our actual physical address before exposing in-place OTA.
  const size_t physical =
      spi_flash_cache2phys(reinterpret_cast<const void *>(reinterpret_cast<uintptr_t>(&hasOtaUpdateSlot)));
  if (physical == SPI_FLASH_CACHE2PHYS_FAIL || physical < running->address ||
      physical - running->address >= running->size)
    return false;
#endif
  return true;
}
} // namespace platform
} // namespace ui
#endif
