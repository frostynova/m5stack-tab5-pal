#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#define PAL_RESOURCE_REQUIRED_COUNT 14

typedef enum {
    PAL_RESOURCE_SD_ERROR,
    PAL_RESOURCE_INCOMPLETE,
    PAL_RESOURCE_READY,
} pal_resource_state_t;

typedef struct {
    pal_resource_state_t state;
    esp_err_t mount_result;
    const char *root;
    size_t present_count;
    size_t required_count;
    bool write_probe_ok;
    bool present[PAL_RESOURCE_REQUIRED_COUNT];
} pal_resource_check_result_t;

const char *pal_resource_required_name(size_t index);
pal_resource_check_result_t pal_resource_check(void);
