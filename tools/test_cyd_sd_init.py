"""Exercise actual SD initialization without disrupting the live LCD DMA bus."""
import argparse
from pathlib import Path
import subprocess
import tempfile


HARNESS = r"""
#undef NDEBUG
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define SPI3_HOST 3
#define SPI_DMA_CH_AUTO 0
#define ESP_OK 0
#define ESP_ERR_TIMEOUT 1
#define ESP_LOGE(...) (++errors)
#define ESP_LOGI(...) ((void)0)
typedef int esp_err_t;
typedef struct { int slot, max_freq_khz; } sdmmc_host_t;
typedef struct {
    int mosi_io_num, miso_io_num, sclk_io_num;
    int quadwp_io_num, quadhd_io_num, max_transfer_sz;
} spi_bus_config_t;
typedef struct { int host_id, gpio_cs; } sdspi_device_config_t;
typedef struct {
    bool format_if_mount_failed;
    int max_files, allocation_unit_size;
} esp_vfs_fat_sdmmc_mount_config_t;
typedef struct { struct { uint64_t capacity; int sector_size; } csd; } sdmmc_card_t;
#define SDSPI_HOST_DEFAULT() ((sdmmc_host_t){0})
#define SDSPI_DEVICE_CONFIG_DEFAULT() ((sdspi_device_config_t){0})
static bool s_mounted, lcd_dma_live;
static int bus_error, mount_error, errors, mounts;
static int spi_bus_initialize(int host, const spi_bus_config_t *bus, int dma) {
    assert(host == SPI3_HOST && dma == SPI_DMA_CH_AUTO);
    assert(bus->mosi_io_num == 23 && bus->miso_io_num == 19 && bus->sclk_io_num == 18);
    return bus_error;
}
/* On the affected ESP32/IDF path, freeing SPI3 stops the existing SPI2 DMA. */
int spi_bus_free(int host) { assert(host == SPI3_HOST); lcd_dma_live = false; return 0; }
static int esp_vfs_fat_sdspi_mount(const char *path, const sdmmc_host_t *host,
    const sdspi_device_config_t *device, const esp_vfs_fat_sdmmc_mount_config_t *config,
    sdmmc_card_t **card) {
    assert(path && host->slot == SPI3_HOST && host->max_freq_khz == 10000);
    assert(device->host_id == SPI3_HOST && device->gpio_cs == 5);
    assert(!config->format_if_mount_failed);
    static sdmmc_card_t actual = {.csd = {.capacity = 2048, .sector_size = 512}};
    *card = &actual; mounts++;
    return mount_error;
}
"""

TEST = r"""
int main(void) {
    for (int outcome = 0; outcome < 3; outcome++) {
        s_mounted = false; lcd_dma_live = true; mounts = errors = 0;
        bus_error = outcome == 0 ? ESP_ERR_TIMEOUT : ESP_OK;
        mount_error = outcome == 1 ? ESP_ERR_TIMEOUT : ESP_OK;
        assert(sd_port_init() == (outcome == 2));
        assert(s_mounted == (outcome == 2));
        assert(errors == (outcome == 2 ? 0 : 1));
        assert(mounts == (outcome == 0 ? 0 : 1));
        assert(lcd_dma_live);
    }
    puts("PASS: SD bus failure, absent/failed card and mounted card preserve LCD DMA; no formatting");
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    source = (Path(__file__).resolve().parent.parent / "firmware" / "main" / "sd_port_cyd.c").read_text()
    init = source[source.index("bool sd_port_init(void)"):source.index("const icon_t *icons_resolve")]
    with tempfile.TemporaryDirectory(prefix="cyd-sd-init-test-") as folder:
        folder = Path(folder)
        test = folder / "test.c"
        test.write_text(HARNESS + init + TEST)
        executable = folder / "test.exe"
        command = [args.cc]
        if Path(args.cc).stem.lower() == "zig":
            command.append("cc")
        subprocess.run(command + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                                  str(test), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
