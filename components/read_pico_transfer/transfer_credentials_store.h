/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 单blob凭据读写，失败不更新公开快照。/ Single-blob credential I/O; failures do not update public snapshots.
 */
#pragma once
#ifndef READ_PICO_TRANSFER_NVS_TEST
#include "nvs.h"
#endif
#include "transfer_policy.h"

static esp_err_t load_credentials(transfer_credentials_t *out) {
    memset(out, 0, sizeof(*out));
    nvs_handle_t handle;
    esp_err_t err = nvs_open("rp_wifi", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    size_t size = sizeof(*out);
    err = nvs_get_blob(handle, "credentials", out, &size);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) { memset(out, 0, sizeof(*out)); return ESP_OK; }
    if (err != ESP_OK || size != sizeof(*out) || !credentials_valid(out)) {
        clear_secret(out, sizeof(*out));
        return err == ESP_OK ? ESP_ERR_INVALID_STATE : err;
    }
    return ESP_OK;
}

static esp_err_t store_credentials(const transfer_credentials_t *next) {
    if (next && !credentials_valid(next)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("rp_wifi", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = next ? nvs_set_blob(handle, "credentials", next, sizeof(*next)) : nvs_erase_key(handle, "credentials");
    if (!next && err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}


// 二改：已知网络库（最多 8 个，按 SSID 去重，先进先淘汰）；活动凭据仍是单个 credentials，
// 库只用于换网时自动挑选与免密重连。/ Mod: a known-network library (at most 8, deduped by
// SSID, FIFO eviction); the active credential stays the single blob, and the library only
// feeds best-network picking and password-free reconnection.
#define TRANSFER_KNOWN_WIFI_MAX 8

static void known_forget(nvs_handle_t handle, const char *ssid) {
    transfer_credentials_t net = {0};
    for (int i = 0; i < TRANSFER_KNOWN_WIFI_MAX; ++i) {
        char key[9];
        snprintf(key, sizeof(key), "known%d", i);
        size_t size = sizeof(net);
        if (nvs_get_blob(handle, key, &net, &size) != ESP_OK || size != sizeof(net)) continue;
        if (!strcmp(net.ssid, ssid)) (void)nvs_erase_key(handle, key);
    }
    clear_secret(&net, sizeof(net));
}

static void known_upsert(const transfer_credentials_t *net) {
    if (!net || !credentials_valid(net)) return;
    nvs_handle_t handle;
    if (nvs_open("rp_wifi", NVS_READWRITE, &handle) != ESP_OK) return;
    known_forget(handle, net->ssid);
    // 满员时淘汰最旧的一条（known0），其余前移。/ Evict the oldest (known0) and shift the rest.
    for (int i = 1; i < TRANSFER_KNOWN_WIFI_MAX; ++i) {
        char from[9], to[9];
        snprintf(from, sizeof(from), "known%d", i);
        snprintf(to, sizeof(to), "known%d", i - 1);
        transfer_credentials_t moved = {0};
        size = sizeof(moved);
        if (nvs_get_blob(handle, from, &moved, &size) == ESP_OK && size == sizeof(moved)) {
            (void)nvs_set_blob(handle, to, &moved, sizeof(moved));
        }
        clear_secret(&moved, sizeof(moved));
        (void)nvs_erase_key(handle, from);
    }
    char key[9];
    snprintf(key, sizeof(key), "known%d", TRANSFER_KNOWN_WIFI_MAX - 1);
    (void)nvs_set_blob(handle, key, net, sizeof(*net));
    (void)nvs_commit(handle);
    nvs_close(handle);
}

static bool known_has_other(const char *ssid) {
    nvs_handle_t handle;
    if (nvs_open("rp_wifi", NVS_READONLY, &handle) != ESP_OK) return false;
    bool other = false;
    for (int i = 0; i < TRANSFER_KNOWN_WIFI_MAX && !other; ++i) {
        char key[9];
        snprintf(key, sizeof(key), "known%d", i);
        transfer_credentials_t net = {0};
        size_t size = sizeof(net);
        if (nvs_get_blob(handle, key, &net, &size) == ESP_OK && size == sizeof(net) &&
            credentials_valid(&net) && (!ssid || strcmp(net.ssid, ssid))) other = true;
        clear_secret(&net, sizeof(net));
    }
    nvs_close(handle);
    return other;
}

static bool known_find(const char *ssid, transfer_credentials_t *out) {
    if (!ssid || !out) return false;
    memset(out, 0, sizeof(*out));
    nvs_handle_t handle;
    if (nvs_open("rp_wifi", NVS_READONLY, &handle) != ESP_OK) return false;
    bool found = false;
    for (int i = 0; i < TRANSFER_KNOWN_WIFI_MAX; ++i) {
        char key[9];
        snprintf(key, sizeof(key), "known%d", i);
        size_t size = sizeof(*out);
        if (nvs_get_blob(handle, key, out, &size) == ESP_OK && size == sizeof(*out) &&
            credentials_valid(out) && !strcmp(out->ssid, ssid)) { found = true; break; }
        memset(out, 0, sizeof(*out));
    }
    nvs_close(handle);
    if (!found) clear_secret(out, sizeof(*out));
    return found;
}
