// hal::sd on the ESP32-S3: SPI2 + DMA, FATFS in the VFS at /sd.     [FIRMWARE.md §6.3, esp32.md]
//
// The file calls are shared/sd_files.cpp -- POSIX against the VFS, identical to the host's.
// This file only brings the card up and down.
//
// SPI mode, not SDMMC: the board wires four lines (esp32.md, IO13/14/21/18) and the S3's SDMMC
// host would want its own.  At 20 MHz one bit wide that is ~2 MB/s of bus, against a stream
// that needs 96 KB/s -- a factor of twenty, which is what the 2 s ring is protecting, not
// throughput.  The SPI master moves every block by DMA (SPI_DMA_CH_AUTO); the CPU only
// queues it.
#include <cstring>

#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include "../../shared/sd_detail.hpp"
#include "clk/board.hpp"
#include "clk/hal/hal.hpp"
#include "clk/log.hpp"
#include "clk/port.hpp"

namespace clk::hal::sd {
namespace {

// 20 MHz, SDMMC_FREQ_DEFAULT.  Every SD card must accept it in SPI mode, and the GPIO-matrix
// routing tops out a little above it.
constexpr int kFreqKhz = SDMMC_FREQ_DEFAULT;
// One DMA transfer.  4 KB is FATFS's multi-sector read for an 8 KB fread plus headroom; the
// SPI driver splits anything longer anyway.
constexpr int kMaxTransfer = 4096 + 8;

port::Mutex g_mx;
bool g_bus = false;
sdmmc_card_t* g_card = nullptr;

bool bus_up() noexcept {
    if (g_bus) return true;
    spi_bus_config_t bus{};
    bus.mosi_io_num = board::kPins.sd_mosi;
    bus.miso_io_num = board::kPins.sd_miso;
    bus.sclk_io_num = board::kPins.sd_sclk;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = kMaxTransfer;
    const esp_err_t e = ::spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        CLK_LOGE(drv_sd, "spi2 bus: %s", ::esp_err_to_name(e));
        return false;
    }
    g_bus = true;
    return true;
}

}  // namespace

Status mount() noexcept {
    if (!board::present(board::Dev::Sd)) return Status::NotPresent;
    port::Lock lk{g_mx};
    if (g_card) return Status::Ok;
    if (!bus_up()) return Status::Failed;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = kFreqKhz;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = SPI2_HOST;
    slot.gpio_cs = static_cast<gpio_num_t>(board::kPins.sd_cs);

    esp_vfs_fat_mount_config_t cfg{};
    // NEVER format.  The card is the user's, and a card that will not mount is one the user
    // should be told about, not one we wipe at 3 a.m. because an alarm wanted a tone.
    cfg.format_if_mount_failed = false;
    cfg.max_files = kMaxOpen + 1;
    cfg.allocation_unit_size = 16 * 1024;
    // The only way FATFS notices a card that was pulled without an unmount.  It costs a
    // status poll per access; at one 8 KB read every 85 ms that is nothing.
    cfg.disk_status_check_enable = true;

    const esp_err_t e = ::esp_vfs_fat_sdspi_mount(kRoot, &host, &slot, &cfg, &g_card);
    if (e != ESP_OK) {
        g_card = nullptr;
        // No card and a card we cannot read look different to the driver and the same to a
        // user.  Both are D16's NotPresent to everything above; the log says which.
        CLK_LOGI(drv_sd, "no card mounted (%s)", ::esp_err_to_name(e));
        return e == ESP_FAIL ? Status::Failed : Status::NotPresent;
    }
    CLK_LOGI(drv_sd, "card %s mounted at %s, %d kHz", g_card->cid.name, kRoot,
             g_card->real_freq_khz);
    return Status::Ok;
}

Status unmount() noexcept {
    port::Lock lk{g_mx};
    if (!g_card) return Status::Ok;
    const esp_err_t e = ::esp_vfs_fat_sdcard_unmount(kRoot, g_card);
    g_card = nullptr;
    return e == ESP_OK ? Status::Ok : Status::Failed;
}

bool mounted() noexcept {
    port::Lock lk{g_mx};
    return g_card != nullptr;
}

Result<Info> info() noexcept {
    port::Lock lk{g_mx};
    if (!g_card) return Result<Info>::bad(Status::NotPresent);
    Info in{};
    if (::esp_vfs_fat_info(kRoot, &in.total_bytes, &in.free_bytes) != ESP_OK)
        return Result<Info>::bad(Status::Failed);
    in.freq_khz = static_cast<uint32_t>(g_card->real_freq_khz);
    std::memcpy(in.name, g_card->cid.name, sizeof in.name);
    in.name[sizeof in.name - 1] = '\0';
    return Result<Info>::good(in);
}

namespace detail {
Status native_path(const char* path, char* out, std::size_t cap) noexcept {
    if (!path || std::strncmp(path, kRoot, 3) != 0 || (path[3] != '/' && path[3] != '\0'))
        return Status::BadArg;
    if (!mounted()) return Status::NotPresent;
    if (std::strlen(path) >= cap) return Status::BadArg;
    std::strcpy(out, path);
    return Status::Ok;
}
}  // namespace detail

}  // namespace clk::hal::sd
