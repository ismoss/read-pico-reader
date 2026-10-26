/* SPDX-License-Identifier: Apache-2.0
 * 中文：DNS 列表解析、地址对持久化、原子换表、短读重试和后台轮询的宿主测试。
 * English: Host tests for DNS parsing, address-pair persistence, atomic list swaps,
 * short-read retries and background polling.
 */
#include <arpa/inet.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "lwip/ip4_addr.h"
#include "settings.h"

static uint8_t saved_index;
static bool saved_pair_known;
static char saved_name[32];
static char saved_main[16];
static char saved_backup[16];
static bool transfer_override;
static char transfer_main[16];
static char transfer_backup[16];
static bool network_ready;

uint8_t app_settings_dns_index(void) {
    return saved_index;
}

bool app_settings_dns_pair_known(void) {
    return saved_pair_known;
}

void app_settings_dns_choice(char *name, size_t name_cap, char *main_ip, size_t main_cap,
                             char *backup_ip, size_t backup_cap) {
    if (name && name_cap) snprintf(name, name_cap, "%s", saved_name);
    if (main_ip && main_cap) snprintf(main_ip, main_cap, "%s", saved_main);
    if (backup_ip && backup_cap) snprintf(backup_ip, backup_cap, "%s", saved_backup);
}

esp_err_t app_settings_set_dns_choice(uint8_t index, const char *name, const char *main_ip,
                                      const char *backup_ip) {
    assert(index < 8);
    assert(name && main_ip && backup_ip);
    saved_index = index;
    snprintf(saved_name, sizeof(saved_name), "%s", name);
    snprintf(saved_main, sizeof(saved_main), "%s", main_ip);
    snprintf(saved_backup, sizeof(saved_backup), "%s", backup_ip);
    saved_pair_known = true;
    return ESP_OK;
}

void read_pico_transfer_get_status(read_pico_transfer_status_t *status) {
    *status = (read_pico_transfer_status_t){
        .mode = READ_PICO_TRANSFER_MODE_STA,
        .network_ready = network_ready,
    };
}

void read_pico_transfer_set_dns_servers(bool enable, const char *main_ip, const char *backup_ip) {
    transfer_override = enable;
    snprintf(transfer_main, sizeof(transfer_main), "%s", enable && main_ip ? main_ip : "");
    snprintf(transfer_backup, sizeof(transfer_backup), "%s", enable && backup_ip ? backup_ip : "");
}

bool ip4addr_aton(const char *text, ip4_addr_t *address) {
    struct in_addr parsed;
    if (!text || !address || inet_pton(AF_INET, text, &parsed) != 1) return false;
    address->addr = parsed.s_addr;
    return true;
}

struct FakeHttp {
    const char *body;
    size_t length;
    size_t offset;
    size_t chunk;
    bool open_ok;
    bool eagain_once;
    bool active;
};

static struct FakeHttp http;
static int64_t now_us;
static void (*pending_task)(void *);

static void reset_http(const char *body, size_t chunk) {
    http = (struct FakeHttp){
        .body = body,
        .length = strlen(body),
        .chunk = chunk,
        .open_ok = true,
        .active = true,
    };
}

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
    assert(config);
    assert(config->disable_auto_redirect);
    assert(config->crt_bundle_attach);
    assert(config->timeout_ms == 7000);
    return &http;
}

esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *name,
                                     const char *value) {
    assert(client == &http);
    assert(name && value);
    return ESP_OK;
}

esp_err_t esp_http_client_open(esp_http_client_handle_t client, int content_length) {
    assert(client == &http);
    assert(content_length == 0);
    return http.open_ok ? ESP_OK : ESP_FAIL;
}

int64_t esp_http_client_fetch_headers(esp_http_client_handle_t client) {
    assert(client == &http);
    return (int64_t)http.length;
}

int esp_http_client_get_status_code(esp_http_client_handle_t client) {
    assert(client == &http);
    return 200;
}

int esp_http_client_read(esp_http_client_handle_t client, char *buffer, int capacity) {
    assert(client == &http);
    assert(capacity > 0);
    if (http.eagain_once) {
        http.eagain_once = false;
        return -ESP_ERR_HTTP_EAGAIN;
    }
    size_t remaining = http.length - http.offset;
    if (!remaining) return 0;
    size_t count = remaining < http.chunk ? remaining : http.chunk;
    if (count > (size_t)capacity) count = (size_t)capacity;
    memcpy(buffer, http.body + http.offset, count);
    http.offset += count;
    now_us += 1000;
    return (int)count;
}

bool esp_http_client_is_complete_data_received(esp_http_client_handle_t client) {
    assert(client == &http);
    return http.offset == http.length;
}

esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client) {
    assert(client == &http);
    http.active = false;
    return ESP_OK;
}

int esp_crt_bundle_attach(void *config) {
    (void)config;
    return ESP_OK;
}

int64_t esp_timer_get_time(void) {
    return now_us += 1000;
}

int xTaskCreate(void (*task)(void *), const char *name, unsigned stack_size, void *arg,
                unsigned priority, void *handle) {
    (void)name;
    (void)priority;
    (void)handle;
    assert(stack_size == 12288);
    pending_task = task;
    assert(!arg);
    return pdPASS;
}

void vTaskDelete(void *task) {
    (void)task;
    pending_task = NULL;
}

void vTaskDelay(unsigned ticks) {
    now_us += (int64_t)ticks * 1000;
}

static void run_pending_task(void) {
    assert(pending_task);
    void (*task)(void *) = pending_task;
    task(NULL);
    assert(!pending_task);
}

#include "../main/mod_dns.c"

static void assert_selected(const char *name, const char *main_ip, const char *backup_ip) {
    assert(saved_pair_known);
    assert(!strcmp(saved_name, name));
    assert(!strcmp(saved_main, main_ip));
    assert(!strcmp(saved_backup, backup_ip));
    assert(transfer_override == (main_ip[0] != 0));
    assert(!strcmp(transfer_main, main_ip));
    assert(!strcmp(transfer_backup, backup_ip));
}

int main(void) {
    char notice[80];
    char body[4096];

    assert(mod_dns_count() == 6);
    assert(mod_dns_saved_index() == 0);
    mod_dns_init();
    assert_selected("跟随路由器 DNS", "", "");

    assert(mod_dns_select(2));
    assert_selected("阿里公共 DNS", "223.5.5.5", "223.6.6.6");

    const char *online_reordered =
        "[{\"name\":\"Cloudflare\",\"main\":\"1.1.1.1\",\"backup\":\"1.0.0.1\"},"
        "{\"name\":\"Quad9\",\"main\":\"9.9.9.9\",\"backup\":\"149.112.112.112\"},"
        "{\"name\":\"Ali moved\",\"main\":\"223.5.5.5\",\"backup\":\"223.6.6.6\"}]";
    assert(apply_online_list(online_reordered, strlen(online_reordered), notice, sizeof(notice)));
    assert(mod_dns_count() == 5);
    assert(mod_dns_saved_index() == 4);
    assert_selected("Ali moved", "223.5.5.5", "223.6.6.6");

    assert(mod_dns_select(2));
    const char *online_without_selection =
        "[{\"name\":\"Quad9\",\"main\":\"9.9.9.9\",\"backup\":\"149.112.112.112\"}]";
    assert(apply_online_list(online_without_selection, strlen(online_without_selection),
                             notice, sizeof(notice)));
    assert(mod_dns_count() == 4);
    assert(mod_dns_saved_index() == 3);
    assert_selected("Cloudflare", "1.1.1.1", "1.0.0.1");

    int before_count = mod_dns_count();
    const char *invalid = "[{\"name\":\"bad\",\"main\":\"999.1.1.1\",\"backup\":\"1.1.1.1\"}]";
    assert(!apply_online_list(invalid, strlen(invalid), notice, sizeof(notice)));
    assert(mod_dns_count() == before_count);
    assert_selected("Cloudflare", "1.1.1.1", "1.0.0.1");

    const char *short_read_body =
        "[{\"name\":\"Cloudflare\",\"main\":\"1.1.1.1\",\"backup\":\"1.0.0.1\"}]";
    reset_http(short_read_body, 3);
    http.eagain_once = true;
    size_t length = 0;
    assert(https_get(PICO_OTA_DNS_URL, body, sizeof(body), &length));
    assert(length == strlen(short_read_body));
    assert(!strcmp(body, short_read_body));
    assert(!http.active);

    network_ready = true;
    reset_http(short_read_body, 5);
    assert(mod_dns_fetch_online_start(notice, sizeof(notice)));
    assert(mod_dns_fetch_online_poll(notice, sizeof(notice)) == MOD_DNS_FETCH_RUNNING);
    run_pending_task();
    assert(mod_dns_fetch_online_poll(notice, sizeof(notice)) == MOD_DNS_FETCH_DONE);
    assert(mod_dns_fetch_online_poll(notice, sizeof(notice)) == MOD_DNS_FETCH_IDLE);
    assert(mod_dns_count() == 3);
    assert_selected("Cloudflare", "1.1.1.1", "1.0.0.1");

    reset_http(short_read_body, 7);
    http.open_ok = false;
    assert(mod_dns_fetch_online_start(notice, sizeof(notice)));
    run_pending_task();
    assert(mod_dns_fetch_online_poll(notice, sizeof(notice)) == MOD_DNS_FETCH_FAILED);
    assert(strstr(notice, "下载失败"));
    assert(mod_dns_count() == 3);

    network_ready = false;
    assert(!mod_dns_fetch_online_start(notice, sizeof(notice)));
    assert(strstr(notice, "请先连接 WiFi"));

    memcpy(s_entries, k_base_entries, sizeof(k_base_entries));
    s_count = 6;
    saved_index = 0;
    snprintf(saved_name, sizeof(saved_name), "%s", "Saved online DNS");
    snprintf(saved_main, sizeof(saved_main), "%s", "1.1.1.1");
    snprintf(saved_backup, sizeof(saved_backup), "%s", "1.0.0.1");
    saved_pair_known = true;
    mod_dns_init();
    assert(mod_dns_count() == 7);
    assert(mod_dns_saved_index() == 6);
    assert(!strcmp(mod_dns_entry(6)->name, "Saved online DNS"));
    assert(!strcmp(mod_dns_saved_name(), "Saved online DNS"));
    assert_selected("Saved online DNS", "1.1.1.1", "1.0.0.1");

    puts("PASS: DNS choices retain their address pair across list reordering and deletion");
    puts("PASS: malformed online data preserves the active list; partial HTTPS reads and background results are handled");
    puts("PASS: a persisted custom pair remains visible after reboot when the online list is unavailable");
}
