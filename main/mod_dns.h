/* SPDX-License-Identifier: Apache-2.0
 * 中文：二改「DNS 设置」的 DNS 选择：内置公共 DNS 列表、在线获取与应用到传输组件。
 * English: DNS choice for the mod DNS settings: a built-in public DNS list, an online
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
/// 当前保存的选择在活动列表中的序号；保存的地址对不在列表时返回 -1。
/// Current saved selection index in the active list; returns -1 when the saved address pair is absent.
int mod_dns_saved_index(void);
/// 当前选择的显示名；列表换序后仍返回保存时的名称。
/// Display label of the saved choice, retaining its saved name when the list is reordered.
const char *mod_dns_saved_name(void);
/// 选择并立即应用；返回是否接受该序号。/ Select and apply at once; false rejects the index.
bool mod_dns_select(int index);
/// 开机时应用已保存的选择。/ Apply the saved choice at boot.
void mod_dns_init(void);
typedef enum {
    MOD_DNS_FETCH_IDLE = 0,
    MOD_DNS_FETCH_RUNNING,
    MOD_DNS_FETCH_DONE,
    MOD_DNS_FETCH_FAILED,
} mod_dns_fetch_state_t;
/// 已联网时后台下载列表；不自行连网或停网，立即返回启动结果。
/// Start a background list download while already online; never connects or stops WiFi itself.
bool mod_dns_fetch_online_start(char *notice, size_t notice_cap);
/// 主线程轮询并将完成结果原子应用到列表；只在 DONE/FAILED 时写 notice。
/// Poll on the UI thread and atomically apply a completed result; notice is written only for DONE/FAILED.
mod_dns_fetch_state_t mod_dns_fetch_online_poll(char *notice, size_t notice_cap);
