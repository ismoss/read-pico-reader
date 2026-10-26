#pragma once

#include <stdbool.h>

// The file manager uses this before opening Transfer to enter USB storage directly.
void app_transfer_request_usb_start(void);

// File manager opens the selected transfer mode directly, without the method picker.
void app_transfer_request_wifi_upload(void);
void app_transfer_request_hotspot_start(void);

// Settings uses this before opening Transfer to enter WiFi provisioning directly.
void app_transfer_request_wifi_setup(void);

// 二改：主循环自动回连的闸门——用户在网络上主动断开、或正停在配网/密码页时为 true，
// 后台不得抢占无线（会被判定为"用户正在操作网络"）。
// Mod: gate for the loop-side auto-reconnect — true while the user disconnected on purpose or
// is sitting on the provisioning / password page, so the background must leave the radio alone.
bool app_transfer_wifi_auto_connect_blocked(void);

// 文件管理打开 WiFi/热点方式选择，选择前不启动传输。
// File manager opens a WiFi/hotspot chooser before starting either service.
void app_transfer_request_method_picker(void);
