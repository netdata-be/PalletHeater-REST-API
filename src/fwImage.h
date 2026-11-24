#pragma once
// Refuses a firmware image for the other chip (ESP32 DevKit <-> ESP32-S3 board) on every
// update path - GitHub install (fwUpdate.h), POST /api/update (httpUpdate.h), espota
// (ota.h), BLE OTA (bleUart.h) - from its first bytes, before anything is erased or written.
// ESP-IDF 4.4 (this core) refuses it too, but only at the end: esp_ota_end() and
// esp_ota_set_boot_partition() (Update.end()) run esp_image_verify(), whose header check
// (bootloader_common_check_chip_validity) fails with "mismatch chip ID" - after the whole
// image was downloaded and the other app slot erased and written. Here it stops at once,
// with a message that says which asset to use. Also refuses a factory image (no app at 0).
#include <sdkconfig.h>
#include <Update.h>
#include <pelletcore.h>

static const uint16_t FW_CHIP_ID = CONFIG_IDF_FIRMWARE_CHIP_ID;   // ESP32 0x0000, ESP32-S3 0x0009

// The start of an image (n bytes, at least pc::IMG_HEAD) against this chip. False = refused:
// the reason is logged, and in *err when given.
static bool __attribute__((noinline)) fwImageOk(const uint8_t* p, size_t n, String* err = nullptr)
{
    char e[128];
    if (pc::firmwareImageCheck(p, n, FW_CHIP_ID, e, sizeof(e))) return true;
    LOGE("update", "image refused: %s", e);
    if (err) *err = e;
    return false;
}

// Update (Arduino core 2.0.17) collects the first 4 KB in its buffer before it erases or
// writes the flash (Updater.cpp, _writeBuffer). Its buffer is private: reached through an
// explicit template instantiation, which may name private members (C++ [temp.explicit]).
struct FwUpdBuf { typedef uint8_t* UpdateClass::*type; friend type fwUpdBufMember(FwUpdBuf); };
template <typename Tag, typename Tag::type M> struct FwPrivate { friend typename Tag::type fwUpdBufMember(Tag) { return M; } };
template struct FwPrivate<FwUpdBuf, &UpdateClass::_buffer>;

// After every Update.write(), with the bytes written so far: checks the image start while it
// is still only in Update's buffer (chunks <= 2 KB: the first check comes before 4 KB). False =
// refused, Update aborted, the reason in err. ArduinoOTA calls it from its progress callback.
static bool fwUpdateChipOk(size_t written, String* err = nullptr)
{
    if (written < pc::IMG_HEAD || Update.progress() != 0 || !Update.isRunning()) return true;
    if (fwImageOk(Update.*fwUpdBufMember(FwUpdBuf()), pc::IMG_HEAD, err)) return true;
    Update.abort();
    return false;
}
