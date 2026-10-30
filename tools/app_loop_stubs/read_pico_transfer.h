/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
static inline void read_pico_transfer_service_poll(void) {}

#include "common.h"
typedef enum {READ_PICO_TRANSFER_MODE_AP=0,READ_PICO_TRANSFER_MODE_STA} read_pico_transfer_mode_t;
typedef enum {READ_PICO_TRANSFER_STOPPED,READ_PICO_TRANSFER_STARTING,READ_PICO_TRANSFER_READY,
    READ_PICO_TRANSFER_UPLOADING,READ_PICO_TRANSFER_ERROR} read_pico_transfer_state_t;
typedef struct {
    read_pico_transfer_state_t state;read_pico_transfer_mode_t mode;char url[64];
    char wifi_ssid[33];bool wifi_configured;bool network_ready;unsigned sta_count;
    char ssid[33];char connected_ssid[33];char cur_name[241];
    size_t cur_bytes,cur_total;unsigned done_count,changed_count;esp_err_t last_error;
} read_pico_transfer_status_t;
typedef struct {
    read_pico_transfer_mode_t mode;bool network_only;bool prefer_known;
} read_pico_transfer_cfg_t;
void read_pico_transfer_get_status(read_pico_transfer_status_t *s);
int read_pico_transfer_get_saved_wifi(char *ssid,bool *configured);
int read_pico_transfer_start(const read_pico_transfer_cfg_t *cfg);
bool read_pico_transfer_try_stop_if_idle(void);
void read_pico_transfer_stop(void);
void app_transfer_wifi_hold_disconnected(void);
void app_transfer_wifi_release_hold(void);
void app_wifi_auto_connect_idle_check(void);
