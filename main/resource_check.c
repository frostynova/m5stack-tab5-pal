#include "resource_check.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bsp/m5stack_tab5.h"
#include "esp_log.h"

static const char *TAG = "pal_resources";

static const char *const REQUIRED_FILES[PAL_RESOURCE_REQUIRED_COUNT] = {
    "abc.mkf",
    "ball.mkf",
    "data.mkf",
    "f.mkf",
    "fbp.mkf",
    "fire.mkf",
    "gop.mkf",
    "map.mkf",
    "mgo.mkf",
    "pat.mkf",
    "rgm.mkf",
    "rng.mkf",
    "sss.mkf",
    "word.dat",
};

const char *pal_resource_required_name(size_t index)
{
    return index < PAL_RESOURCE_REQUIRED_COUNT ? REQUIRED_FILES[index] : NULL;
}

static bool file_is_nonempty(const char *root, const char *name, off_t *size)
{
    char path[128];
    const int written = snprintf(path, sizeof(path), "%s/%s", root, name);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        return false;
    }

    struct stat info;
    if (stat(path, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size <= 0) {
        return false;
    }

    if (size != NULL) {
        *size = info.st_size;
    }
    return true;
}

static size_t count_present_files(const char *root, bool *present, bool log_files)
{
    size_t count = 0;
    for (size_t i = 0; i < PAL_RESOURCE_REQUIRED_COUNT; ++i) {
        off_t size = 0;
        present[i] = file_is_nonempty(root, REQUIRED_FILES[i], &size);
        if (present[i]) {
            ++count;
            if (log_files) {
                ESP_LOGI(TAG, "found %-9s (%lld bytes)", REQUIRED_FILES[i], (long long)size);
            }
        } else if (log_files) {
            ESP_LOGW(TAG, "missing or empty: %s/%s", root, REQUIRED_FILES[i]);
        }
    }
    return count;
}

static bool probe_directory_write(const char *root)
{
    static const char marker[] = "SDLPAL_TAB5_WRITE_PROBE_V1";
    char path[128];
    char readback[sizeof(marker)];
    const int written = snprintf(path, sizeof(path), "%s/.sdlpal-write-test", root);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        ESP_LOGE(TAG, "write probe path is too long: %s", root);
        return false;
    }

    /* Remove a stale probe left by a reset during an earlier check. */
    if (unlink(path) != 0 && errno != ENOENT) {
        ESP_LOGW(TAG, "could not remove stale write probe %s: %s", path, strerror(errno));
    }

    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        ESP_LOGE(TAG, "SD write probe open failed for %s: %s", path, strerror(errno));
        return false;
    }

    bool ok = fwrite(marker, 1, sizeof(marker), fp) == sizeof(marker);
    if (!ok) {
        ESP_LOGE(TAG, "SD write probe short write for %s: %s", path, strerror(errno));
    } else if (fflush(fp) != 0) {
        ESP_LOGE(TAG, "SD write probe flush failed for %s: %s", path, strerror(errno));
        ok = false;
    } else if (fsync(fileno(fp)) != 0) {
        ESP_LOGE(TAG, "SD write probe sync failed for %s: %s", path, strerror(errno));
        ok = false;
    }

    if (fclose(fp) != 0) {
        ESP_LOGE(TAG, "SD write probe close failed for %s: %s", path, strerror(errno));
        ok = false;
    }

    if (ok) {
        fp = fopen(path, "rb");
        if (fp == NULL) {
            ESP_LOGE(TAG, "SD write probe readback open failed for %s: %s", path, strerror(errno));
            ok = false;
        } else {
            const size_t count = fread(readback, 1, sizeof(readback), fp);
            if (count != sizeof(readback) || memcmp(readback, marker, sizeof(marker)) != 0) {
                ESP_LOGE(TAG, "SD write probe readback mismatch for %s (%zu/%zu bytes)",
                         path, count, sizeof(readback));
                ok = false;
            }
            if (fclose(fp) != 0) {
                ESP_LOGE(TAG, "SD write probe readback close failed for %s: %s", path, strerror(errno));
                ok = false;
            }
        }
    }

    if (unlink(path) != 0) {
        ESP_LOGW(TAG, "could not remove SD write probe %s: %s", path, strerror(errno));
        ok = false;
    }

    if (ok) {
        ESP_LOGI(TAG, "SD write/sync/readback probe passed in %s", root);
    }
    return ok;
}

pal_resource_check_result_t pal_resource_check(void)
{
    pal_resource_check_result_t result = {
        .state = PAL_RESOURCE_SD_ERROR,
        .mount_result = ESP_FAIL,
        .root = BSP_SD_MOUNT_POINT "/pal",
        .required_count = PAL_RESOURCE_REQUIRED_COUNT,
    };

    ESP_LOGI(TAG, "mounting SD card at %s", BSP_SD_MOUNT_POINT);
    /* SDLPal keeps the core MKF archives open for the lifetime of the game.
     * The BSP convenience mount only allows five concurrent files, which is
     * enough for the detector but not for the engine. */
    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 20,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_host_t sdmmc_host = {0};
    bsp_sdcard_get_sdmmc_host(SDMMC_HOST_SLOT_0, &sdmmc_host);
    /* Some cards are marginal at 40 MHz on the Tab5 routing and returned
     * intermittent short reads during sustained MKF traffic. 20 MHz remains
     * ample for SDLPal and was stable with the tested card. Keep this policy
     * in the application instead of modifying the managed BSP. */
    sdmmc_host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    bsp_sdcard_cfg_t sdcard_config = {
        .mount = &mount_config,
        .host = &sdmmc_host,
    };
    ESP_LOGI(TAG, "using SDMMC at %d kHz", sdmmc_host.max_freq_khz);
    result.mount_result = bsp_sdcard_sdmmc_mount(&sdcard_config);
    if (result.mount_result != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(result.mount_result));
        return result;
    }

    /* Prefer a dedicated directory, but also accept files copied directly to
     * the card root so a first hardware test does not require a precise layout. */
    const char *const candidates[] = {
        BSP_SD_MOUNT_POINT "/pal",
        BSP_SD_MOUNT_POINT,
    };

    size_t best_count = 0;
    const char *best_root = candidates[0];
    bool candidate_present[PAL_RESOURCE_REQUIRED_COUNT];

    for (size_t root_index = 0; root_index < sizeof(candidates) / sizeof(candidates[0]); ++root_index) {
        const size_t count = count_present_files(candidates[root_index], candidate_present, false);
        if (count > best_count) {
            best_count = count;
            best_root = candidates[root_index];
        }
    }

    result.root = best_root;
    result.present_count = count_present_files(result.root, result.present, true);
    result.state = result.present_count == result.required_count
                       ? PAL_RESOURCE_READY
                       : PAL_RESOURCE_INCOMPLETE;

    if (result.state == PAL_RESOURCE_READY) {
        ESP_LOGI(TAG, "PAL core resource set is complete in %s", result.root);
        result.write_probe_ok = probe_directory_write(result.root);
    } else {
        ESP_LOGW(TAG, "PAL core resource set is incomplete: %zu/%zu files in %s",
                 result.present_count, result.required_count, result.root);
    }

    return result;
}
