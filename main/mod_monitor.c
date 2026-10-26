/* SPDX-License-Identifier: Apache-2.0
 * 中文：二改「远程监看」：局域网网页看屏 + 触摸注入。默认关闭且每次开机都关闭；
 * 手动开启会生成只存 RAM 的 128 位密钥，画面和触摸接口校验该密钥；静态网页壳不含隐私。
 * 联网就绪后起停独立 httpd（8123 端口，不与传书的 80 冲突）。画面按触摸芯片
 * 坐标系出图，浏览器点哪儿注入哪儿；帧缓冲读取只在两路数据接口之外由主循环换指针，
 * 多个画面请求共用同一 BMP 时用互斥锁串行生成与发送。密钥不写 NVS、不写日志。
 * English: Mod "remote monitor": a LAN web page mirroring the screen and injecting touch.
 * Off by default and after every boot. Manual enable creates a RAM-only 128-bit token; the
 * frame and touch endpoints verify it, while the static page shell contains no private data. An independent httpd (port 8123, no clash
 * with transfer's 80) follows network readiness. The image uses the touch chip's coordinate
 * space. A mutex serializes BMP generation and sending between frame clients. The token is
 * never stored in NVS or written to logs.
 */
#include "mod_monitor.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "cst836u.h"
#include "epdiy.h"
#include "esp_wifi.h"
#include "read_pico_transfer.h"

#define MOD_MONITOR_PORT 8123
// 物理 1216x684；逻辑/触摸 684x1216。画面按半分辨率出图（342x608）：点按由网页按比例
// 换算，不受影响；载荷缩到 1/4，省电模式下也能在下一轮轮询前送完。
// Physical 1216x684; logical/touch 684x1216. The frame serves at half resolution (342x608):
// the page maps taps by ratio, unaffected, and the quarter-size payload completes well within
// one polling period even under WiFi power save.
#define MON_LOGICAL_W 684
#define MON_LOGICAL_H 1216
#define MON_W (MON_LOGICAL_W / 2)
#define MON_H (MON_LOGICAL_H / 2)
#define MON_STRIDE ((MON_W + 3) & ~3)
#define MON_BMP_HEADER 1078
#define MON_BMP_SIZE (MON_BMP_HEADER + (size_t)MON_STRIDE * MON_H)
#define MOD_MONITOR_IDLE_MS 600000ll
#define MON_TOKEN_BYTES 16
#define MON_TOKEN_HEX (MON_TOKEN_BYTES * 2)

static const char *TAG = "monitor";

static httpd_handle_t s_http;
static bool s_enabled;
static bool s_frame_ready;
static uint8_t *s_bmp;              // BMP 头 + 按文件行序的 8bit 影像 / header + file-order 8-bit frame
static const uint8_t *s_fb;         // UI 渲染帧缓冲（app_loop 每轮记录）/ render framebuffer from app_loop
static char s_base_url[48];         // "http://ip:8123"（联网就绪时）/ service URL while online
static char s_url[96];              // 服务地址 + 一次性密钥 / service URL plus session token
static char s_token[MON_TOKEN_HEX + 1];
static portMUX_TYPE s_token_mux = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t s_bmp_lock;
static int64_t s_next_check_ms;
static int64_t s_last_seen_ms;      // 最近一次 HTTP 请求（空闲自动关用）/ last HTTP request
static portMUX_TYPE s_seen_mux = portMUX_INITIALIZER_UNLOCKED;

// 注入会话：down → move… → up；up 消费一轮后结束。/ Injection session: down, moves, then one
// consumed up sample ends it.
static struct {
    SemaphoreHandle_t lock;
    bool active, down_pending, up_pending;
    uint16_t x, y;
} s_inject;

bool mod_monitor_enabled(void) { return s_enabled; }

static void note_seen(void) {
    const int64_t now = esp_timer_get_time() / 1000;
    portENTER_CRITICAL(&s_seen_mux);
    s_last_seen_ms = now;
    portEXIT_CRITICAL(&s_seen_mux);
}

static int64_t last_seen(void) {
    portENTER_CRITICAL(&s_seen_mux);
    const int64_t value = s_last_seen_ms;
    portEXIT_CRITICAL(&s_seen_mux);
    return value;
}

static void make_token(void) {
    static const char hex[] = "0123456789abcdef";
    uint8_t bytes[MON_TOKEN_BYTES];
    esp_fill_random(bytes, sizeof(bytes));
    portENTER_CRITICAL(&s_token_mux);
    for (size_t i = 0; i < sizeof(bytes); ++i) {
        s_token[i * 2] = hex[bytes[i] >> 4];
        s_token[i * 2 + 1] = hex[bytes[i] & 0x0f];
    }
    s_token[MON_TOKEN_HEX] = 0;
    portEXIT_CRITICAL(&s_token_mux);
    memset(bytes, 0, sizeof(bytes));
}

bool mod_monitor_set_enabled(bool on) {
    if (s_enabled == on) return false;
    s_enabled = on;
    if (on) make_token();
    else {
        portENTER_CRITICAL(&s_token_mux);
        memset(s_token, 0, sizeof(s_token));
        portEXIT_CRITICAL(&s_token_mux);
    }
    s_url[0] = 0;
    s_base_url[0] = 0;
    s_next_check_ms = 0;  // 下一轮 tick 立即按开关起停 / the next tick applies the switch at once
    note_seen();
    return true;
}

void mod_monitor_url(char *buf, size_t cap) {
    if (!cap) return;
    snprintf(buf, cap, "%s", (s_enabled && s_url[0]) ? s_url : "");
}

void mod_monitor_base_url(char *buf, size_t cap) {
    if (!cap) return;
    snprintf(buf, cap, "%s", (s_enabled && s_base_url[0]) ? s_base_url : "");
}

void mod_monitor_token(char *buf, size_t cap) {
    if (!cap) return;
    char token[sizeof(s_token)];
    portENTER_CRITICAL(&s_token_mux);
    memcpy(token, s_token, sizeof(token));
    portEXIT_CRITICAL(&s_token_mux);
    snprintf(buf, cap, "%s", (s_enabled && token[0]) ? token : "");
}

static void stop_server(void) {
    if (!s_http) return;
    httpd_stop(s_http);
    s_http = NULL;
    // 服务停了就恢复默认省电；启用期间以 PS_NONE 保证画面传输时限内送达。
    // Restore default power save once serving stops; PS_NONE while up keeps frames on schedule.
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    if (s_bmp) { heap_caps_free(s_bmp); s_bmp = NULL; }
    s_frame_ready = false;
    s_base_url[0] = 0;
    s_url[0] = 0;
    if (s_inject.lock) {
        xSemaphoreTake(s_inject.lock, portMAX_DELAY);
        s_inject.active = s_inject.down_pending = s_inject.up_pending = false;
        xSemaphoreGive(s_inject.lock);
    }
    ESP_LOGI(TAG, "stopped");
}

static bool token_matches(const char *candidate) {
    if (!candidate || strlen(candidate) != MON_TOKEN_HEX) return false;
    char expected[MON_TOKEN_HEX + 1];
    portENTER_CRITICAL(&s_token_mux);
    memcpy(expected, s_token, sizeof(expected));
    portEXIT_CRITICAL(&s_token_mux);
    if (!expected[0]) return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < MON_TOKEN_HEX; ++i)
        diff |= (uint8_t)candidate[i] ^ (uint8_t)expected[i];
    return diff == 0;
}

static bool request_authorized(httpd_req_t *req) {
    char key[40];
    if (httpd_req_get_hdr_value_str(req, "X-Pico-Key", key, sizeof(key)) != ESP_OK) return false;
    return token_matches(key);
}

static void private_headers(httpd_req_t *req, bool page) {
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    if (page)
        httpd_resp_set_hdr(
            req, "Content-Security-Policy",
            "default-src 'none'; img-src 'self' blob:; connect-src 'self'; "
            "style-src 'unsafe-inline'; script-src 'unsafe-inline'; "
            "base-uri 'none'; frame-ancestors 'none'");
}

static esp_err_t deny_request(httpd_req_t *req) {
    private_headers(req, false);
    httpd_resp_set_status(req, "403 Forbidden");
    return httpd_resp_sendstr(req, "forbidden");
}

static esp_err_t page_handler(httpd_req_t *req) {
    static const char page[] =
        "<!doctype html><html><head><meta charset=utf-8>"
        "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
        "<title>Pico</title><style>body{margin:0;background:#1c1c1c;color:#ddd;"
        "font-family:sans-serif;text-align:center}#s{display:block;margin:0 auto;"
        "max-width:100vw;max-height:calc(100vh - 34px);width:auto;height:auto;"
        "touch-action:none;image-rendering:pixelated}#t{font-size:13px;padding:6px}"
        "</style></head><body>"
        "<div id=t>...</div><img id=s alt=\"\">"
        "<script>const i=document.getElementById('s'),t=document.getElementById('t');"
        // 宽高都留 auto + max 约束：图片按比例缩进窗口，元素框正好等于渲染后的图，触摸换算才准。
        // 原来 width:100% 会把 342x608 的帧拉到整个窗口宽，桌面浏览器只露出最上面几分之一，
        // Ctrl+- 缩放也救不了（宽度跟着视口走）。
        // width/height stay auto under max constraints so the picture scales proportionally inside
        // the window and its box equals the rendered image (tap mapping depends on that). The old
        // width:100% blew a 342x608 frame up to the window width, so a desktop browser showed only
        // the top fraction and zooming out could not help because the width followed the viewport.
        // 密钥放 URL #fragment，浏览器不会把它发给服务器，也不会进请求行日志；页面脚本再通过
        // 请求头交给 /screen 与 /touch。页面本身不含隐私数据，可以公开拿到静态壳。
        // The token lives in the URL fragment, which browsers never send to the server or request
        // logs. The page passes it to /screen and /touch in a request header. The public static
        // shell contains no private data.
        "const k=new URLSearchParams(location.hash.slice(1)).get('k')||'';"
        "let d=false,busy=false,mv=0,wd=0,obu=0;"
        "function h(){return {'X-Pico-Key':k}}"
        "function g(){if(busy)return;if(!/^[0-9a-f]{32}$/.test(k))"
        "{t.textContent='key missing';return;}"
        "busy=true;const c=new AbortController();"
        "clearTimeout(wd);wd=setTimeout(()=>c.abort(),8000);"
        "fetch('/screen?n='+Date.now(),{cache:'no-store',headers:h(),signal:c.signal})"
        ".then(r=>{if(!r.ok)throw Error();return r.blob()})"
        ".then(b=>{clearTimeout(wd);if(obu)URL.revokeObjectURL(obu);"
        "obu=URL.createObjectURL(b);i.src=obu;})"
        ".catch(()=>{clearTimeout(wd);busy=false;t.textContent='waiting';"
        "setTimeout(g,1500);});"
        "}"
        // 看门狗：请求被浏览器丢弃时既不会 onload 也不会 onerror，busy 会永久卡住不再刷新
        // （手机切后台再回来就是这样）。八秒没有结果就当作失败重来。
        // Watchdog: a discarded request fires neither onload nor onerror and leaves busy stuck, so
        // nothing refreshes any more (that is what a phone does after being backgrounded). Give up
        // after eight seconds and retry.
        "i.onload=()=>{clearTimeout(wd);busy=false;t.textContent='live';setTimeout(g,600);};"
        "i.onerror=()=>{clearTimeout(wd);busy=false;t.textContent='waiting';setTimeout(g,1500);};"
        "document.addEventListener('visibilitychange',"
        "()=>{if(!document.hidden&&!busy)g();});"
        "function p(e){const b=i.getBoundingClientRect();return{x:Math.max(0,Math.min(683,"
        "Math.round((e.clientX-b.left)/b.width*684))),y:Math.max(0,Math.min(1215,"
        "Math.round((e.clientY-b.top)/b.height*1216)))};}"
        "function s(a,x,y){fetch('/touch?t='+a+'&x='+x+'&y='+y,"
        "{cache:'no-store',headers:h()}).catch(()=>{});}"
        "i.addEventListener('pointerdown',e=>{d=true;i.setPointerCapture(e.pointerId);"
        "const q=p(e);s(1,q.x,q.y);e.preventDefault();});"
        "i.addEventListener('pointermove',e=>{if(!d)return;const n=Date.now();"
        "if(n-mv<80)return;mv=n;const q=p(e);s(2,q.x,q.y);e.preventDefault();});"
        "i.addEventListener('pointerup',e=>{if(!d)return;d=false;const q=p(e);"
        "s(0,q.x,q.y);e.preventDefault();});"
        "i.addEventListener('contextmenu',e=>e.preventDefault());"
        "g();"
        "</script></body></html>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    private_headers(req, true);
    return httpd_resp_send(req, page, sizeof(page) - 1);
}

// 8bit 灰度 BMP 文件头（含 256 项灰阶调色板，共 1078 字节）：分配时写一次即可复用。
// The 8-bit grayscale BMP file header (with the 256-entry gray palette, 1078 bytes total):
// written once at allocation and reused for every request.
// BITMAPFILEHEADER(14) + BITMAPINFOHEADER(40)：字段偏移逐字节对齐规范，
// 保留位与数据偏移必须落位（此前把 1078 写进了 biSize，浏览器永远判定非法图）。
// BITMAPFILEHEADER(14) + BITMAPINFOHEADER(40): every field at its spec offset — the reserved
// words and the pixel-data offset must land exactly (1078 previously went into biSize, so
// browsers always rejected the image).
static void write_bmp_header(void) {
    uint8_t *h = s_bmp;
    const uint32_t body = (uint32_t)MON_STRIDE * MON_H, file = MON_BMP_HEADER + body;
    // 先把 54 字节头整块清零：biPlanes(26-27) 与 biBitCount(28-29) 是 16 位字段，
    // 下面只写低字节，高字节若留着 malloc 的脏数据（实测 0xFF），头就变成
    // biPlanes=0xFF01 / biBitCount=0xFF08，任何解码器都判非法——浏览器只表现为破图。
    // Zero the whole 54-byte header first: biPlanes (26-27) and biBitCount (28-29) are 16-bit
    // fields written low byte only below, so leftover malloc garbage (0xFF measured on device)
    // turned them into 0xFF01 / 0xFF08 and every decoder rejected the file as a broken image.
    memset(h, 0, 54);
    h[0] = 'B'; h[1] = 'M';
    for (int i = 0; i < 4; ++i) h[2 + i] = (uint8_t)(file >> (8 * i));   // 文件大小 / file size
    for (int i = 6; i < 10; ++i) h[i] = 0;                               // 保留位 / reserved
    for (int i = 0; i < 4; ++i) h[10 + i] = (uint8_t)(1078 >> (8 * i));  // 像素数据偏移 / pixel offset
    for (int i = 0; i < 4; ++i) h[14 + i] = (uint8_t)(40 >> (8 * i));    // biSize = 40
    for (int i = 0; i < 4; ++i) h[18 + i] = (uint8_t)(MON_W >> (8 * i));
    for (int i = 0; i < 4; ++i) h[22 + i] = (uint8_t)(MON_H >> (8 * i));
    h[26] = 1;                                                            // biPlanes
    h[28] = 8;                                                            // biBitCount
    for (int i = 30; i < 54; ++i) h[i] = 0;                               // 压缩/尺寸/分辨率/色数 / rest
    for (int i = 0; i < 256; ++i) {
        uint8_t *p = h + 54 + i * 4;
        p[0] = p[1] = p[2] = (uint8_t)i;                                  // B=G=R 灰阶 / gray
        p[3] = 0;
    }
}

// 物理网格写进 BMP：logical x = 683-py, y = px；文件行自底向上，直接按行序生成。
// Physical grid into BMP: logical x = 683-py, y = px; bottom-up file rows, generated in place.
static void build_frame(void) {
    static const int pw = 1216, ph = 684, stride = pw / 2;
    uint8_t *body = s_bmp + MON_BMP_HEADER;
    // 影像行自底向上；半分辨率最近邻取样：影像 (ix,iy) = 逻辑 (2ix, 2iy)。
    // Bottom-up image rows with 2x nearest-neighbour sampling: image (ix,iy) = logical (2ix, 2iy).
    for (int r = 0; r < MON_H; ++r) {
        const int ly = (MON_H - 1 - r) * 2;
        uint8_t *out = body + (size_t)r * MON_STRIDE;
        for (int ix = 0; ix < MON_W; ++ix) {
            const int py = ph - 1 - ix * 2, px = ly;
            const uint8_t byte = s_fb[(size_t)py * stride + (px >> 1)];
            const uint8_t nib = (px & 1) ? (byte >> 4) : (byte & 0x0F);
            out[ix] = (uint8_t)(nib * 17);
        }
    }
}

static esp_err_t screen_handler(httpd_req_t *req) {
    if (!request_authorized(req)) return deny_request(req);
    // 空闲计时放在最前面：取帧失败（503）也算"有人在看"，否则一直失败 10 分钟后
    // 会被自己的空闲自动关撤掉开关。
    // Refresh the idle timer before any early return: a failing frame is still a viewer, or the
    // ten-minute idle auto-off would clear the switch on a client that is actively polling.
    note_seen();
    if (!s_bmp || !s_fb || !s_frame_ready || !s_bmp_lock) {
        private_headers(req, false);
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "frame not ready");
    }
    xSemaphoreTake(s_bmp_lock, portMAX_DELAY);
    build_frame();
    httpd_resp_set_type(req, "image/bmp");
    private_headers(req, false);
    const esp_err_t err = httpd_resp_send(req, (const char *)s_bmp, (size_t)MON_BMP_SIZE);
    xSemaphoreGive(s_bmp_lock);
    return err;
}

static esp_err_t touch_handler(httpd_req_t *req) {
    if (!request_authorized(req)) return deny_request(req);
    char query[128] = "", value[16] = "";
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) goto bad;
    int type = 0, x = -1, y = -1;
    if (httpd_query_key_value(query, "t", value, sizeof(value)) == ESP_OK) type = atoi(value);
    if (httpd_query_key_value(query, "x", value, sizeof(value)) == ESP_OK) x = atoi(value);
    if (httpd_query_key_value(query, "y", value, sizeof(value)) == ESP_OK) y = atoi(value);
    // 注入坐标仍是逻辑系 684x1216，与画面分辨率无关。/ Injected coordinates stay in the
    // 684x1216 logical space regardless of the frame resolution.
    if (x < 0 || x >= MON_LOGICAL_W || y < 0 || y >= MON_LOGICAL_H || type < 0 || type > 2) goto bad;
    if (!s_inject.lock) goto bad;
    note_seen();
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
    private_headers(req, false);
    return httpd_resp_sendstr(req, "ok");
bad:
    private_headers(req, false);
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_sendstr(req, "bad request");
}

static void update_urls(const read_pico_transfer_status_t *st) {
    char base[sizeof(s_base_url)] = "";
    char token[sizeof(s_token)];
    portENTER_CRITICAL(&s_token_mux);
    memcpy(token, s_token, sizeof(token));
    portEXIT_CRITICAL(&s_token_mux);
    if (st->network_ready && !strncmp(st->url, "http://", 7) && token[0])
        snprintf(base, sizeof(base), "%.40s:%d", st->url, MOD_MONITOR_PORT);
    if (strcmp(base, s_base_url) != 0 || (base[0] && !s_url[0])) {
        snprintf(s_base_url, sizeof(s_base_url), "%s", base);
        if (base[0])
            snprintf(s_url, sizeof(s_url), "%s/#k=%s", base, token);
        else
            s_url[0] = 0;
    }
}

static void start_server(void) {
    if (s_http || s_bmp) stop_server();
    if (!s_bmp_lock) s_bmp_lock = xSemaphoreCreateMutex();
    if (!s_inject.lock) s_inject.lock = xSemaphoreCreateMutex();
    if (!s_bmp_lock || !s_inject.lock) {
        ESP_LOGW(TAG, "lock alloc failed");
        return;
    }
    s_bmp = heap_caps_malloc(MON_BMP_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_bmp) { ESP_LOGW(TAG, "frame buffer alloc failed"); return; }
    write_bmp_header();
    httpd_config_t http = HTTPD_DEFAULT_CONFIG();
    http.server_port = MOD_MONITOR_PORT;
    http.max_uri_handlers = 4;
    // 浏览器对一个 host 最多开 6 条连接：2 条时页面、图片、触摸三路会互相把在途响应
    // 挤掉（LRU 关闭），表现为图片半截/闪烁。4 条留余量，仍远低于 LWIP_MAX_SOCKETS(10) - 3。
    // A browser opens up to six connections per host: with two, the page, the image and the
    // touch calls evict each other's in-flight responses (LRU close) as half-sent frames.
    // Four leaves headroom and stays well under LWIP_MAX_SOCKETS(10) - 3.
    http.max_open_sockets = 4;
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
    note_seen();
    esp_wifi_set_ps(WIFI_PS_NONE);
    read_pico_transfer_status_t st;
    read_pico_transfer_get_status(&st);
    update_urls(&st);
    ESP_LOGI(TAG, "serving on port %d", MOD_MONITOR_PORT);
}

void mod_monitor_tick(const uint8_t *framebuffer) {
    s_fb = framebuffer;
    const int64_t now = esp_timer_get_time() / 1000;
    if (now < s_next_check_ms) return;
    s_next_check_ms = now + 1000;
    read_pico_transfer_status_t st;
    read_pico_transfer_get_status(&st);
    // 空闲自动关：10 分钟没有任何网页请求就停服并撤销开关（用户要求“不用就关掉”）。
    // Idle auto-off: ten minutes without any web request stops serving and clears the switch.
    if (s_enabled && s_http && now - last_seen() >= MOD_MONITOR_IDLE_MS) {
        ESP_LOGI(TAG, "idle timeout, auto disable");
        mod_monitor_set_enabled(false);
    }
    const bool want = s_enabled && st.network_ready;
    if (want && !s_http) start_server();
    else if (!want && s_http) stop_server();
    if (s_http) update_urls(&st);
}

bool mod_monitor_inject(cst836u_touch_t *touch) {
    if (!s_inject.lock) return false;
    xSemaphoreTake(s_inject.lock, portMAX_DELAY);
    if (!s_inject.active) {
        xSemaphoreGive(s_inject.lock);
        return false;
    }
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
    s_enabled = false;
    portENTER_CRITICAL(&s_token_mux);
    memset(s_token, 0, sizeof(s_token));
    portEXIT_CRITICAL(&s_token_mux);
    s_base_url[0] = 0;
    s_url[0] = 0;
}
