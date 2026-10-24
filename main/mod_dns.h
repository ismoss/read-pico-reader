/* SPDX-License-Identifier: Apache-2.0
 * 中文：二改「特殊功能」的 DNS 选择：内置公共 DNS 列表、在线获取与应用到传输组件。
 * English: DNS choice for the mod "special features": a built-in public DNS list, an online
 * fetch and applying the selection to the transfer stack.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>

#define MOD_DNS_MAX 8

typedef struct {
    char name[24];
    char main_ip[16];
    char backup_ip[16];
} mod_dns_entry_t;

/// 条目 0 恒为「跟随路由器」（不覆盖）；1 为默认公共对；2 起为内置或在线列表。
/// Entry 0 is always the router passthrough (no override); 1 the default public pair; 2+ built-ins or the online list.
int mod_dns_count(void);
const mod_dns_entry_t *mod_dns_entry(int index);
/// 当前保存的选择（NVS，越界回退到 1）。/ The persisted selection (NVS, out-of-range falls back to 1).
int mod_dns_saved_index(void);
/// 选择并立即应用；返回是否接受该序号。/ Select and apply at once; false rejects the index.
bool mod_dns_select(int index);
/// 开机时应用已保存的选择。/ Apply the saved choice at boot.
void mod_dns_init(void);
/// 阻塞式在线获取列表（照 OTA 的联网路径）；结果写入 notice，返回是否成功。
/// Blocking online fetch mirroring the OTA path; fills the notice and reports success.
bool mod_dns_fetch_online(char *notice, size_t notice_cap);
