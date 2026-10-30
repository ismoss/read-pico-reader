/* 中文：调度测试替身。/ English: Scheduler test shim. */
#pragma once
#include "common.h"
bool read_pico_sd_needs_recovery(void);
esp_err_t read_pico_sd_start_recovery(void);
void read_pico_sd_poll_health(void);
