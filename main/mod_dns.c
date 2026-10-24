/*
 * SPDX-FileCopyrightText: 2026 ismoss
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：二改 DNS 选择的数据面：内置公共列表、在线列表（自建源 dns.json）与应用到传输组件。
 * English: Data plane of the mod DNS choice: the built-in public list, the online list
 * (dns.json on the self-hosted origin) and applying the selection to the transfer stack.
 * 冻结：不做任何 UI；联网仅显式触发；列表最多 MOD_DNS_MAX 项，条目 0 恒为路由器直通。
 * Frozen: no UI here; networking only on explicit request; at most MOD_DNS_MAX entries with entry 0 always the router passthrough.
 */
#include "mod_dns.h"

#include <stdint.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/ip4_addr.h"
#include "read_pico_transfer.h"
#include "settings.h"
#include "ota_online.h"

static const char* TAG = "read_pico";

static mod_dns_entry_t s_entries[MOD_DNS_MAX] = {
    {"跟随路由器 DNS", "", ""},
    {"公共 DNS 默认", "223.5.5.5", "119.29.29.29"},
    {"阿里公共 DNS", "223.5.5.5", "223.6.6.6"},
    {"腾讯公共 DNS", "119.29.29.29", "119.28.28.28"},
    {"114 公共 DNS", "114.114.114.114", "114.114.115.115"},
    {"谷歌公共 DNS", "8.8.8.8", "8.8.4.4"},
};
static int s_count = 6;

int mod_dns_count(void) { return s_count; }

const mod_dns_entry_t* mod_dns_entry(int index) {
    if (index < 0 || index >= s_count || index >= MOD_DNS_MAX) index = index == 0 ? 0 : 1;
    return &s_entries[index];
}

int mod_dns_saved_index(void) {
    int index = app_settings_dns_index();
    return index >= 0 && index < s_count ? index : 1;
}

// 把当前选择压给传输组件；条目 0 = 停用覆盖（跟随路由器）。
// Push the selection to the transfer stack; entry 0 disables the override (router passthrough).
static void apply_selection(int index) {
    const mod_dns_entry_t* entry = mod_dns_entry(index);
    bool override = entry->main_ip[0] != 0;
    read_pico_transfer_set_dns_servers(override, entry->main_ip, entry->backup_ip);
}

bool mod_dns_select(int index) {
    if (index < 0 || index >= s_count) return false;
    app_settings_set_dns_index((uint8_t)index);
    apply_selection(index);
    ESP_LOGI(TAG, "dns select %d %s", index, s_entries[index].name);
    return true;
}

void mod_dns_init(void) { apply_selection(mod_dns_saved_index()); }

// 仿 ota_online 的联网准备：优先复用已连接的 STA，否则用保存的凭据短连，等待就绪并按需对时。
// Mirror ota_online's network prep: reuse a connected STA or briefly connect with saved
// credentials, wait for readiness and sync time when needed for HTTPS validation.
static bool ensure_online(bool* owned, char* notice, size_t notice_cap) {
    read_pico_transfer_status_t status;
    read_pico_transfer_get_status(&status);
    if (!(status.mode == READ_PICO_TRANSFER_MODE_STA && status.network_ready)) {
        char ssid[33];
        bool saved = false;
        if (read_pico_transfer_get_saved_wifi(ssid, &saved) != ESP_OK || !saved) {
            snprintf(notice, notice_cap, "请先在 WiFi 设置中连接网络");
            return false;
        }
        read_pico_transfer_stop();
        read_pico_transfer_cfg_t cfg = {.mode = READ_PICO_TRANSFER_MODE_STA, .network_only = true};
        *owned = true;
        if (read_pico_transfer_start(&cfg) != ESP_OK) {
            snprintf(notice, notice_cap, "WiFi 连接失败，请重试");
            return false;
        }
    }
    int64_t deadline = esp_timer_get_time() + 30000000;
    for (;;) {
        read_pico_transfer_service_poll();
        read_pico_transfer_get_status(&status);
        if (status.mode == READ_PICO_TRANSFER_MODE_STA && status.network_ready) {
            if (time(NULL) < 1704067200) {
                uint32_t utc;
                if (read_pico_transfer_sync_time_online(&utc) != ESP_OK) {
                    snprintf(notice, notice_cap, "网络对时失败，请对时后重试");
                    return false;
                }
            }
            return true;
        }
        if (status.state == READ_PICO_TRANSFER_ERROR || esp_timer_get_time() >= deadline) break;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    snprintf(notice, notice_cap, "网络连接超时，请重试");
    return false;
}

// 单次 HTTPS GET 到内存；按 OTA 客户端规则不跟随重定向。
// One HTTPS GET into memory; redirects are refused exactly like the OTA client.
static bool https_get(const char* url, char* body, size_t cap, size_t* length) {
    esp_http_client_config_t cfg = {
        .url = url, .timeout_ms = 15000, .buffer_size = 1024, .buffer_size_tx = 512,
        .crt_bundle_attach = esp_crt_bundle_attach, .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return false;
    bool ok = false;
    esp_http_client_set_header(client, "User-Agent", PICO_OTA_CAPABILITY);
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    if (esp_http_client_open(client, 0) == ESP_OK) {
        int64_t content_length = esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        if (status == 200 && content_length > 0 && (size_t)content_length < cap) {
            int read = esp_http_client_read(client, body, (int)content_length);
            if (read == (int)content_length) {
                body[read] = 0;
                *length = (size_t)read;
                ok = true;
            }
        }
    }
    esp_http_client_cleanup(client);
    return ok;
}

bool mod_dns_fetch_online(char* notice, size_t notice_cap) {
    bool owned = false;
    snprintf(notice, notice_cap, "正在联网获取 DNS 列表…");
    if (!ensure_online(&owned, notice, notice_cap)) goto done;
    {
        static char body[4096];
        size_t length = 0;
        if (!https_get(PICO_OTA_DNS_URL, body, sizeof(body), &length)) {
            snprintf(notice, notice_cap, "下载失败，保留内置列表");
            goto done;
        }
        cJSON* root = cJSON_ParseWithLength(body, length);
        if (!root || !cJSON_IsArray(root)) {
            snprintf(notice, notice_cap, "列表解析失败，保留内置列表");
            if (root) cJSON_Delete(root);
            goto done;
        }
        // 在线条目覆盖索引 2 起的位置；0/1（路由器、默认公共对）固定不动。
        // Online entries replace slots from index 2; 0/1 (router, default public pair) stay fixed.
        int filled = 2;
        const cJSON* item;
        cJSON_ArrayForEach(item, root) {
            if (filled >= MOD_DNS_MAX) break;
            const cJSON* name = cJSON_GetObjectItemCaseSensitive(item, "name");
            const cJSON* main_ip = cJSON_GetObjectItemCaseSensitive(item, "main");
            const cJSON* backup_ip = cJSON_GetObjectItemCaseSensitive(item, "backup");
            ip4_addr_t main_addr, backup_addr;
            if (!cJSON_IsString(name) || !cJSON_IsString(main_ip) || !cJSON_IsString(backup_ip) ||
                !name->valuestring || !main_ip->valuestring || !backup_ip->valuestring ||
                strlen(name->valuestring) >= sizeof(s_entries[0].name) ||
                !ip4addr_aton(main_ip->valuestring, &main_addr) ||
                !ip4addr_aton(backup_ip->valuestring, &backup_addr)) continue;
            mod_dns_entry_t* entry = &s_entries[filled++];
            snprintf(entry->name, sizeof(entry->name), "%s", name->valuestring);
            snprintf(entry->main_ip, sizeof(entry->main_ip), "%s", main_ip->valuestring);
            snprintf(entry->backup_ip, sizeof(entry->backup_ip), "%s", backup_ip->valuestring);
        }
        cJSON_Delete(root);
        if (filled > 2) {
            s_count = filled;
            // 选中项可能已被覆盖出界；压回合法范围并重新应用。
            // The saved pick may have fallen outside the new list; clamp and re-apply it.
            apply_selection(mod_dns_saved_index());
            snprintf(notice, notice_cap, "已更新 %d 项在线 DNS", filled - 2);
        } else {
            snprintf(notice, notice_cap, "在线列表为空，保留内置列表");
        }
    }
done:
    if (owned) read_pico_transfer_stop();
    return notice[0] && strncmp(notice, "已更新", 3) == 0;
}
