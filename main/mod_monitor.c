/* SPDX-License-Identifier: Apache-2.0
 * 中文：二改「远程监看」：局域网网页看屏 + 触摸注入。默认关闭；开关持久化在 NVS，
 * 联网就绪后自动起停独立 httpd（8123 端口，不与传书的 80 冲突）。无鉴权——仅建议
 * 家庭等可信局域网使用；画面按触摸芯片坐标系出图，浏览器点哪儿注入哪儿，任何
 * 旋转下都落在正确的控件上。画面直接读渲染帧缓冲，极端时机可能读到撕裂帧，
 * 下一次轮询自然纠正。
 * English: Mod "remote monitor": a LAN web page mirroring the screen and injecting touch.
 * Off by default; the switch persists in NVS and an independent httpd (port 8123, no clash
 * with transfer's 80) starts once the network is ready. No authentication — trusted LANs
 * only. The image uses the touch chip's coordinate space, so a browser tap lands on the
 * right control at any rotation. The frame is read straight from the render buffer; a torn
 * frame at the worst moment self-corrects on the next poll.
 */
#include "mod_monitor.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

#include "cst836u.h"
#include "epdiy.h"
#include "read_pico_transfer.h"

#define MOD_MONITOR_PORT 8123
#define MOD_MONITOR_NVS_NS "rp_monitor"
// 物理 1216x684；逻辑/触摸 684x1216。/ Physical 1216x684; logical/touch 684x1216.
#define MON_W 684
#define MON_H 1216
#define MON_BMP_HEADER 1078
#define MON_BMP_SIZE (MON_BMP_HEADER + (size_t)MON_W * MON_H)

static const char *TAG = "monitor";

static httpd_handle_t s_http;
static bool s_enabled;
static bool s_frame_ready;
static uint8_t *s_bmp;              // BMP 头 + 按文件行序的 8bit 影像 / header + file-order 8-bit frame
static const uint8_t *s_fb;         // UI 渲染帧缓冲（app_loop 每轮记录）/ render framebuffer from app_loop
static char s_url[48];              // "http://ip:8123"（联网就绪时）/ visit URL while online
static int64_t s_next_check_ms;

// 注入会话：down → move… → up；up 消费一轮后结束。/ Injection session: down, moves, then one
// consumed up sample ends it.
static struct {
    SemaphoreHandle_t lock;
    bool active, down_pending, up_pending;
    uint16_t x, y;
} s_inject;

bool mod_monitor_enabled(void) { return s_enabled; }

bool mod_monitor_set_enabled(bool on) {
    if (s_enabled == on) return false;
    s_enabled = on;
    nvs_handle_t h;
    if (nvs_open(MOD_MONITOR_NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "on", s_enabled ? 1 : 0);
        nvs_commit(h);
        nvs_close(h);
    }
    s_next_check_ms = 0;  // 下一轮 tick 立即按开关起停 / the next tick applies the switch at once
    return true;
}

void mod_monitor_url(char *buf, size_t cap) {
    snprintf(buf, cap, "%s", (s_enabled && s_url[0]) ? s_url : "");
}

static void stop_server(void) {
    if (!s_http) return;
    httpd_stop(s_http);
    s_http = NULL;
    if (s_bmp) { heap_caps_free(s_bmp); s_bmp = NULL; }
    s_frame_ready = false;
    s_url[0] = 0;
    if (s_inject.lock) {
        xSemaphoreTake(s_inject.lock, portMAX_DELAY);
        s_inject.active = s_inject.down_pending = s_inject.up_pending = false;
        xSemaphoreGive(s_inject.lock);
    }
    ESP_LOGI(TAG, "stopped");
}

static esp_err_t page_handler(httpd_req_t *req) {
    static const char page[] =
        "<!doctype html><html><head><meta charset=utf-8>"
        "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
        "<title>Pico</title><style>body{margin:0;background:#1c1c1c;color:#ddd;"
        "font-family:sans-serif;text-align:center}#s{width:100%;touch-action:none;"
        "image-rendering:pixelated}#t{font-size:13px;padding:6px}</style></head><body>"
        "<div id=t>...</div><img id=s src=\"/screen\">"
        "<script>const i=document.getElementById('s'),t=document.getElementById('t');"
        "let d=false,l=0;function r(){const n=Date.now();if(n-l<350)return;l=n;"
        "i.onload=()=>t.textContent='live';i.onerror=()=>t.textContent='waiting';"
        "i.src='/screen?n='+n;}setInterval(r,900);r();"
        "function p(e){const b=i.getBoundingClientRect();return{x:Math.max(0,Math.min(683,"
        "Math.round((e.clientX-b.left)/b.width*684))),y:Math.max(0,Math.min(1215,"
        "Math.round((e.clientY-b.top)/b.height*1216)))};}"
        "function s(a,x,y){fetch('/touch?t='+a+'&x='+x+'&y='+y).then(r);}"
        "i.addEventListener('pointerdown',e=>{d=true;i.setPointerCapture(e.pointerId);"
        "const q=p(e);s(1,q.x,q.y);e.preventDefault();});"
        "i.addEventListener('pointermove',e=>{if(!d)return;const q=p(e);s(2,q.x,q.y);"
        "e.preventDefault();});"
        "i.addEventListener('pointerup',e=>{if(!d)return;d=false;const q=p(e);"
        "s(0,q.x,q.y);e.preventDefault();});"
        "i.addEventListener('contextmenu',e=>e.preventDefault());"
        "</script></body></html>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, page, sizeof(page) - 1);
}

// 物理网格写进 BMP：logical x = 683-py, y = px；文件行自底向上，直接按行序生成。
// Physical grid into BMP: logical x = 683-py, y = px; bottom-up file rows, generated in place.
static void build_frame(void) {
    static const int pw = 1216, ph = 684, stride = pw / 2;
    uint8_t *body = s_bmp + MON_BMP_HEADER;
    for (int r = 0; r < MON_H; ++r) {
        const int y_l = MON_H - 1 - r;          // 逻辑 y / logical y
        uint8_t *out = body + (size_t)r * MON_W;
        for (int x = 0; x < MON_W; ++x) {
            const int py = ph - 1 - x, px = y_l;
            const uint8_t byte = s_fb[(size_t)py * stride + (px >> 1)];
            const uint8_t nib = (px & 1) ? (byte >> 4) : (byte & 0x0F);
            out[x] = (uint8_t)(nib * 17);
        }
    }
}

static esp_err_t screen_handler(httpd_req_t *req) {
    if (!s_bmp || !s_fb || !s_frame_ready) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "frame not ready");
    }
    build_frame();
    httpd_resp_set_type(req, "image/bmp");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)s_bmp, (size_t)MON_BMP_SIZE);
}

static esp_err_t touch_handler(httpd_req_t *req) {
    char query[64] = "", value[16] = "";
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) goto bad;
    int type = 0, x = -1, y = -1;
    if (httpd_query_key_value(query, "t", value, sizeof(value)) == ESP_OK) type = atoi(value);
    if (httpd_query_key_value(query, "x", value, sizeof(value)) == ESP_OK) x = atoi(value);
    if (httpd_query_key_value(query, "y", value, sizeof(value)) == ESP_OK) y = atoi(value);
    if (x < 0 || x >= MON_W || y < 0 || y >= MON_H || type < 0 || type > 2) goto bad;
    if (!s_inject.lock) goto bad;
    xSemaphoreTake(s_inject.lock, portMAX_DELAY);
    if (type == 1) {
        s_inject.active = true;
        s_inject.down_pending = true;
        s_inject.up_pending = false;
        s_inject.x = (uint16_t)x;
        s_inject.y = (uint16_t)y;
    } else if (s_inject.active) {
        if (type == 2) { s_inject.x = (uint16_t)x; s_inject.y = (uint16_t)y; }
        else s_inject.up_pending = true;
    }
    xSemaphoreGive(s_inject.lock);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, "ok");
bad:
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_sendstr(req, "bad request");
}

static void start_server(void) {
    if (s_http || s_bmp) stop_server();
    s_bmp = heap_caps_malloc(MON_BMP_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_bmp) { ESP_LOGW(TAG, "frame buffer alloc failed"); return; }
    if (!s_inject.lock) s_inject.lock = xSemaphoreCreateMutex();
    httpd_config_t http = HTTPD_DEFAULT_CONFIG();
    http.server_port = MOD_MONITOR_PORT;
    http.max_uri_handlers = 4;
    http.max_open_sockets = 2;
    http.stack_size = 8192;
    http.recv_wait_timeout = 10;
    http.send_wait_timeout = 20;
    http.lru_purge_enable = true;
    if (httpd_start(&s_http, &http) != ESP_OK) {
        ESP_LOGW(TAG, "httpd start failed");
        s_http = NULL;
        heap_caps_free(s_bmp);
        s_bmp = NULL;
        return;
    }
    const httpd_uri_t routes[] = {
        { .uri = "/", .method = HTTP_GET, .handler = page_handler },
        { .uri = "/screen", .method = HTTP_GET, .handler = screen_handler },
        { .uri = "/touch", .method = HTTP_GET, .handler = touch_handler },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(*routes); ++i)
        if (httpd_register_uri_handler(s_http, &routes[i]) != ESP_OK) {
            stop_server();
            return;
        }
    s_frame_ready = true;
    read_pico_transfer_status_t st;
    read_pico_transfer_get_status(&st);
    if (st.network_ready && !strncmp(st.url, "http://", 7))
        snprintf(s_url, sizeof(s_url), "%.40s:%d", st.url, MOD_MONITOR_PORT);
    ESP_LOGI(TAG, "serving at %s", s_url);
}

void mod_monitor_tick(const uint8_t *framebuffer) {
    s_fb = framebuffer;
    const int64_t now = esp_timer_get_time() / 1000;
    if (now < s_next_check_ms) return;
    s_next_check_ms = now + 1000;
    read_pico_transfer_status_t st;
    read_pico_transfer_get_status(&st);
    const bool want = s_enabled && st.network_ready;
    if (want && !s_http) start_server();
    else if (!want && s_http) stop_server();
    if (s_http && !s_url[0]) {
        read_pico_transfer_get_status(&st);
        if (st.network_ready && !strncmp(st.url, "http://", 7))
            snprintf(s_url, sizeof(s_url), "%.40s:%d", st.url, MOD_MONITOR_PORT);
    }
}

bool mod_monitor_inject(cst836u_touch_t *touch) {
    if (!s_inject.lock || !s_inject.active) return false;
    xSemaphoreTake(s_inject.lock, portMAX_DELAY);
    const bool up = s_inject.up_pending, down = s_inject.down_pending;
    const uint16_t x = s_inject.x, y = s_inject.y;
    if (down) s_inject.down_pending = false;
    if (up) s_inject.active = false;
    xSemaphoreGive(s_inject.lock);
    memset(touch, 0, sizeof(*touch));
    touch->touched = !up;
    touch->count = 1;
    touch->x = x;
    touch->y = y;
    (void)down;
    return true;
}

void mod_monitor_init(void) {
    nvs_handle_t h;
    if (nvs_open(MOD_MONITOR_NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        uint8_t on = 0;
        nvs_get_u8(h, "on", &on);
        nvs_close(h);
        s_enabled = on != 0;
    }
}
