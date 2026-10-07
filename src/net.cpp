#include "net.h"

#include <string.h>

#include <string>

#include "adxl375.h"
#include "board.h"
#include "clock.h"
#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hardreset.h"
#include "lvgl_port.h"
#include "lwip/sockets.h"
#include "report.h"
#include "settings.h"
#include "shock.h"
#include "store.h"
#include "ui.h"
#include "version.h"

static const char *TAG = "net";

// Avalik (mitte privaatne) aadress: Android jätab captive-kontrolli vahele, kui DNS vastab privaatse IP-ga (SimCam kogemus)
#define AP_IP_STR "4.3.2.1"
#define AP_IP_B 4, 3, 2, 1

extern const char index_html_start[] asm("_binary_index_html_start");

static esp_netif_t *s_ap, *s_sta;
static std::string s_ssid;
static char s_sta_ip[16];
static volatile bool s_ota;
static volatile int s_clients;

std::string net_ap_ssid(void) { return s_ssid; }
std::string net_sta_ip(void) { return s_sta_ip; }
int net_ap_clients(void) { return s_clients; }
bool net_ota_active(void) { return s_ota; }

// ---------------- DNS (kõik nimed -> AP IP) ----------------
static void dns_task(void *)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(53);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (sock < 0 || bind(sock, (sockaddr *)&a, sizeof(a)) != 0) {
        ESP_LOGE(TAG, "DNS bind ebaõnnestus");
        vTaskDelete(nullptr);
        return;
    }
    uint8_t buf[512], out[512];
    while (true) {
        sockaddr_in from = {};
        socklen_t fl = sizeof(from);
        int n = recvfrom(sock, buf, sizeof(buf), 0, (sockaddr *)&from, &fl);
        if (n < 17 || (buf[2] & 0x80) || (buf[4] << 8 | buf[5]) != 1) continue;
        int i = 12;
        while (i < n && buf[i]) {
            if (buf[i] & 0xC0) { i = n; break; }
            i += buf[i] + 1;
        }
        if (i + 5 > n) continue;
        i++;
        uint16_t qtype = buf[i] << 8 | buf[i + 1];
        int qend = i + 4;
        bool ans = qtype == 1 || qtype == 255;
        memcpy(out, buf, qend);
        out[2] = 0x80 | (buf[2] & 0x01);
        out[3] = 0x80;
        out[6] = 0;
        out[7] = ans ? 1 : 0;
        out[8] = out[9] = out[10] = out[11] = 0;
        int o = qend;
        if (ans) {
            const uint8_t rr[] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 30, 0, 4, AP_IP_B};
            memcpy(out + o, rr, sizeof(rr));
            o += sizeof(rr);
        }
        sendto(sock, out, o, 0, (sockaddr *)&from, fl);
    }
}

// ---------------- HTTP abi ----------------
static void set_json(httpd_req_t *r) { httpd_resp_set_type(r, "application/json; charset=utf-8"); }

static int qint(httpd_req_t *req, const char *key, int def)
{
    char q[128], v[24];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK && httpd_query_key_value(q, key, v, sizeof(v)) == ESP_OK)
        return atoi(v);
    return def;
}

static std::string jesc(const char *s)
{
    std::string o;
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') o += '\\';
        if ((unsigned char)*s < 0x20) continue;
        o += *s;
    }
    return o;
}

static esp_err_t send_str(httpd_req_t *r, const std::string &s)
{
    // suured vastused tükkidena
    const size_t CH = 16384;
    for (size_t o = 0; o < s.size(); o += CH)
        if (httpd_resp_send_chunk(r, s.data() + o, s.size() - o < CH ? s.size() - o : CH) != ESP_OK) return ESP_FAIL;
    return httpd_resp_send_chunk(r, nullptr, 0);
}

static esp_err_t index_get(httpd_req_t *r)
{
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    return httpd_resp_send(r, index_html_start, HTTPD_RESP_USE_STRLEN);
}

// Telefonide internetikontrollid ja tundmatud aadressid -> portaal
static esp_err_t redirect(httpd_req_t *r)
{
    httpd_resp_set_status(r, "302 Found");
    httpd_resp_set_hdr(r, "Location", "http://" AP_IP_STR "/");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    return httpd_resp_send(r, "", 0);
}

static esp_err_t status_get(httpd_req_t *r)
{
    char b[900];
    size_t used, total;
    store_usage(&used, &total);
    const SeriesInfo &si = store_open_series();
    snprintf(b, sizeof(b),
             "{\"fw\":\"%s\",\"time_valid\":%s,\"time\":\"%s\",\"sensor\":%s,\"rate\":%.0f,\"odr\":%d,"
             "\"overruns\":%lu,\"i2c_err\":%lu,\"used_kb\":%u,\"total_kb\":%u,\"open\":%lu,\"shots\":%u,"
             "\"state\":%d,\"sta_ip\":\"%s\",\"uptime\":%lu,\"i2c\":\"%s\",\"adxl\":\"%s\"}",
             FW_VERSION, clock_valid() ? "true" : "false", clock_fmt(clock_epoch()).c_str(),
             adxl_present() ? "true" : "false", adxl_measured_rate(), adxl_odr(), (unsigned long)adxl_overruns(),
             (unsigned long)adxl_i2c_errors(), (unsigned)(used / 1024), (unsigned)(total / 1024),
             (unsigned long)(store_has_open() ? si.id : 0), store_has_open() ? si.shots : 0, (int)shock_state(),
             s_sta_ip, (unsigned long)clock_uptime_s(), board_i2c_diag(), adxl_id_text());
    set_json(r);
    return httpd_resp_send(r, b, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t time_post(httpd_req_t *r)
{
    char b[64] = {};
    int n = httpd_req_recv(r, b, sizeof(b) - 1);
    long long ms = 0;
    int tz = 0;
    if (n <= 0 || sscanf(b, "%lld,%d", &ms, &tz) < 1 || ms < 1700000000000LL) {
        httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "vale aeg");
        return ESP_FAIL;
    }
    bool was = clock_valid();
    clock_set(ms, tz);
    store_backfill_time();
    if (!was) {
        ESP_LOGI(TAG, "kell seadistatud: %s", clock_fmt(clock_epoch()).c_str());
        LvGuard g;
        ui_refresh();
    }
    set_json(r);
    return httpd_resp_send(r, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t tests_get(httpd_req_t *r)
{
    auto list = store_list();
    std::string o = "[";
    for (auto &si : list) {
        bool pass;
        const char *v = report_verdict(si, &pass);
        char b[512];
        snprintf(b, sizeof(b),
                 "%s{\"id\":%lu,\"object\":\"%s\",\"serial\":\"%s\",\"oper\":\"%s\",\"start\":\"%s\",\"preset\":\"%s\","
                 "\"a\":%.1f,\"td\":%.2f,\"shots\":%u,\"plan\":%u,\"passed\":%u,\"verdict\":\"%s\",\"open\":%s}",
                 o.size() > 1 ? "," : "", (unsigned long)si.id, jesc(si.object).c_str(), jesc(si.serial).c_str(),
                 jesc(si.oper).c_str(), si.start_epoch ? clock_fmt(si.start_epoch).c_str() : "", preset_name(si.preset),
                 si.a_nom, si.td_nom, si.shots, si.shots_per_dir * 6, si.passed, v, si.closed ? "false" : "true");
        o += b;
    }
    o += "]";
    set_json(r);
    return send_str(r, o);
}

static esp_err_t report_get(httpd_req_t *r)
{
    int id = qint(r, "id", 0);
    bool csv = qint(r, "csv", 0) != 0;
    std::string out;
    bool ok = csv ? report_csv(id, out) : report_pdf(id, out);
    if (!ok) {
        httpd_resp_send_err(r, HTTPD_404_NOT_FOUND, "seeriat pole");
        return ESP_FAIL;
    }
    char fn[96];
    SeriesInfo si;
    std::vector<ShotRec> sh;
    store_load(id, si, sh);
    std::string date = si.start_epoch ? clock_fmt(si.start_epoch).substr(0, 10) : "kuupaev";
    snprintf(fn, sizeof(fn), "attachment; filename=\"shock_test_%04d_%s.%s\"", id, date.c_str(), csv ? "csv" : "pdf");
    httpd_resp_set_type(r, csv ? "text/csv; charset=utf-8" : "application/pdf");
    httpd_resp_set_hdr(r, "Content-Disposition", fn);
    return send_str(r, out);
}

static esp_err_t delete_post(httpd_req_t *r)
{
    int id = qint(r, "id", 0);
    bool was_open = store_has_open() && store_open_series().id == (uint32_t)id;
    bool ok = store_delete(id);
    if (was_open) {
        shock_arm(false);
        LvGuard g;
        ui_refresh();
    }
    set_json(r);
    return httpd_resp_send(r, ok ? "{\"ok\":true}" : "{\"ok\":false}", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t ota_info(httpd_req_t *r)
{
    const esp_app_desc_t *d = esp_app_get_description();
    const esp_partition_t *run = esp_ota_get_running_partition();
    char b[256];
    snprintf(b, sizeof(b), "{\"version\":\"%s\",\"build\":\"%s %s\",\"partition\":\"%s\",\"idf\":\"%s\"}", FW_VERSION,
             d->date, d->time, run ? run->label : "?", d->idf_ver);
    set_json(r);
    return httpd_resp_send(r, b, HTTPD_RESP_USE_STRLEN);
}

// Püsivara üleslaadimine: keha = firmware.bin
static esp_err_t ota_post(httpd_req_t *r)
{
    const esp_partition_t *part = esp_ota_get_next_update_partition(nullptr);
    if (!part || r->content_len <= 0 || r->content_len > (int)part->size) {
        httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "vale faili suurus");
        return ESP_FAIL;
    }
    if (s_ota) {
        httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "uuendus juba käib");
        return ESP_FAIL;
    }
    s_ota = true;
    shock_arm(false);
    ESP_LOGI(TAG, "püsivara %d baiti -> %s", r->content_len, part->label);
    {
        LvGuard g;
        ui_ota_progress(0, "Püsivara uuendus: alustan ...");
    }
    esp_ota_handle_t h;
    esp_err_t err = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &h);
    char *buf = (char *)malloc(8192);
    int left = r->content_len, last = -1;
    bool first = true;
    while (err == ESP_OK && left > 0) {
        int n = httpd_req_recv(r, buf, left < 8192 ? left : 8192);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) { err = ESP_FAIL; break; }
        if (first) {
            first = false;
            // ESP32 rakenduse pildi maagiline bait
            if ((uint8_t)buf[0] != 0xE9) { err = ESP_ERR_INVALID_ARG; break; }
        }
        err = esp_ota_write(h, buf, n);
        left -= n;
        int pct = (int)((int64_t)(r->content_len - left) * 100 / r->content_len);
        if (pct != last) {
            last = pct;
            char t[64];
            snprintf(t, sizeof(t), "Püsivara uuendus: %d %%", pct);
            LvGuard g;
            ui_ota_progress(pct, t);
        }
    }
    free(buf);
    if (err == ESP_OK) err = esp_ota_end(h);
    else esp_ota_abort(h);
    if (err == ESP_OK) err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uuendus ebaõnnestus: %s", esp_err_to_name(err));
        s_ota = false;
        {
            LvGuard g;
            ui_ota_progress(-1, err == ESP_ERR_INVALID_ARG ? "Vale fail (pole ESP32 püsivara)" : "Uuendus ebaõnnestus");
        }
        httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR,
                            err == ESP_ERR_INVALID_ARG ? "vale fail (pole ESP32 püsivara)" : esp_err_to_name(err));
        return ESP_FAIL;
    }
    {
        LvGuard g;
        ui_ota_progress(100, "Valmis! Taaskäivitan ...");
    }
    set_json(r);
    httpd_resp_send(r, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    vTaskDelay(pdMS_TO_TICKS(1500));
    hard_restart();
}

// --- arendus: ekraanipilt, puudutus, simulatsioon ---
static esp_err_t shot_get(httpd_req_t *r)
{
    size_t len = 0;
    uint8_t *rle = lvport_screenshot_rle(&len);
    if (!rle) return httpd_resp_send_500(r);
    httpd_resp_set_type(r, "application/octet-stream");
    esp_err_t e = httpd_resp_send(r, (const char *)rle, len);
    free(rle);
    return e;
}

static esp_err_t tap_get(httpd_req_t *r)
{
    lvport_inject_tap(qint(r, "x", 0), qint(r, "y", 0));
    return httpd_resp_send(r, "ok", 2);
}

static esp_err_t sim_get(httpd_req_t *r)
{
    float a = qint(r, "a10", (int)(g_set.peak_g * 10)) / 10.0f;
    float td = qint(r, "td100", (int)(g_set.td_ms * 100)) / 100.0f;
    adxl_simulate(a, td, qint(r, "axis", 2), qint(r, "noise100", 30) / 100.0f);
    return httpd_resp_send(r, "ok", 2);
}

// I2C katsetus: /dev/i2c?addr=83&reg=0&n=1&hz=100000&sep=1&wr=-1  (sep=1: STOP kirjutuse ja lugemise vahel; wr>=0: kirjuta reg=wr)
static esp_err_t i2c_get(httpd_req_t *r)
{
    int addr = qint(r, "addr", 0x53), reg = qint(r, "reg", 0), n = qint(r, "n", 1), hz = qint(r, "hz", 100000);
    int sep = qint(r, "sep", 0), wv = qint(r, "wr", -1);
    if (n < 1) n = 1;
    if (n > 32) n = 32;
    i2c_device_config_t dc = {};
    dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dc.device_address = addr;
    dc.scl_speed_hz = hz;
    i2c_master_dev_handle_t d;
    char out[256];
    if (i2c_master_bus_add_device(board_i2c_bus(), &dc, &d) != ESP_OK) return httpd_resp_send(r, "add_device viga", HTTPD_RESP_USE_STRLEN);
    uint8_t rg = reg, buf[32] = {};
    esp_err_t e;
    if (wv >= 0) {
        uint8_t b2[2] = {(uint8_t)reg, (uint8_t)wv};
        e = i2c_master_transmit(d, b2, 2, 50);
        snprintf(out, sizeof(out), "kirjutus reg 0x%02X=0x%02X: %s", reg, wv, esp_err_to_name(e));
    } else {
        if (sep) {
            e = i2c_master_transmit(d, &rg, 1, 50);
            if (e == ESP_OK) e = i2c_master_receive(d, buf, n, 50);
        } else {
            e = i2c_master_transmit_receive(d, &rg, 1, buf, n, 50);
        }
        int o = snprintf(out, sizeof(out), "0x%02X reg 0x%02X %s:", addr, reg, esp_err_to_name(e));
        for (int i = 0; i < n && o < (int)sizeof(out) - 4; i++) o += snprintf(out + o, sizeof(out) - o, " %02X", buf[i]);
    }
    i2c_master_bus_rm_device(d);
    return httpd_resp_send(r, out, HTTPD_RESP_USE_STRLEN);
}

// /dev/odr?hz=1600 -> salvesta ja taaskäivita
static esp_err_t odr_get(httpd_req_t *r)
{
    int hz = qint(r, "hz", 1600);
    g_set.odr = hz >= 3200 ? 3200 : hz >= 1600 ? 1600 : 800;
    settings_save();
    httpd_resp_send(r, "ok", 2);
    vTaskDelay(pdMS_TO_TICKS(300));
    hard_restart();
}

static esp_err_t reboot_get(httpd_req_t *r)
{
    httpd_resp_send(r, "ok", 2);
    vTaskDelay(pdMS_TO_TICKS(300));
    hard_restart();
}

static void reg(httpd_handle_t s, const char *uri, httpd_method_t m, esp_err_t (*h)(httpd_req_t *))
{
    httpd_uri_t u = {};
    u.uri = uri;
    u.method = m;
    u.handler = h;
    httpd_register_uri_handler(s, &u);
}

static void http_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 12288;  // OTA kirjutab flash-i: pinu sisemises RAM-is
    cfg.max_uri_handlers = 24;
    cfg.recv_wait_timeout = 20;
    cfg.send_wait_timeout = 20;
    cfg.lru_purge_enable = true;
    cfg.max_open_sockets = 8;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    httpd_handle_t s;
    if (httpd_start(&s, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd ei käivitunud");
        return;
    }
    reg(s, "/", HTTP_GET, index_get);
    reg(s, "/api/status", HTTP_GET, status_get);
    reg(s, "/api/time", HTTP_POST, time_post);
    reg(s, "/api/tests", HTTP_GET, tests_get);
    reg(s, "/api/report", HTTP_GET, report_get);
    reg(s, "/api/delete", HTTP_POST, delete_post);
    reg(s, "/api/fw", HTTP_GET, ota_info);
    reg(s, "/api/ota", HTTP_POST, ota_post);
    reg(s, "/dev/shot", HTTP_GET, shot_get);
    reg(s, "/dev/tap", HTTP_GET, tap_get);
    reg(s, "/dev/sim", HTTP_GET, sim_get);
    reg(s, "/dev/reboot", HTTP_GET, reboot_get);
    reg(s, "/dev/i2c", HTTP_GET, i2c_get);
    reg(s, "/dev/odr", HTTP_GET, odr_get);
    reg(s, "/*", HTTP_GET, redirect);  // viimane: captive-kontrollid ja muu
    ESP_LOGI(TAG, "HTTP: http://" AP_IP_STR "/");
}

// ---------------- WiFi ----------------
static void on_event(void *, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_AP_STACONNECTED) s_clients++;
        else if (id == WIFI_EVENT_AP_STADISCONNECTED && s_clients > 0) s_clients--;
        else if (id == WIFI_EVENT_STA_START && g_set.sta_ssid[0]) esp_wifi_connect();
        else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            s_sta_ip[0] = 0;
            if (g_set.sta_ssid[0]) esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto *e = (ip_event_got_ip_t *)data;
        esp_ip4addr_ntoa(&e->ip_info.ip, s_sta_ip, sizeof(s_sta_ip));
        ESP_LOGI(TAG, "klientvõrk: %s", s_sta_ip);
    }
    if (base == WIFI_EVENT && (id == WIFI_EVENT_AP_STACONNECTED || id == WIFI_EVENT_AP_STADISCONNECTED)) {
        LvGuard g;
        ui_refresh();
    }
}

void net_apply_settings(void)
{
    wifi_config_t ap = {};
    strlcpy((char *)ap.ap.ssid, s_ssid.c_str(), sizeof(ap.ap.ssid));
    ap.ap.ssid_len = s_ssid.size();
    ap.ap.channel = 6;
    ap.ap.max_connection = 4;
    if (strlen(g_set.ap_pass) >= 8) {
        strlcpy((char *)ap.ap.password, g_set.ap_pass, sizeof(ap.ap.password));
        ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        ap.ap.authmode = WIFI_AUTH_OPEN;
    }
    esp_wifi_set_config(WIFI_IF_AP, &ap);
    wifi_config_t sta = {};
    strlcpy((char *)sta.sta.ssid, g_set.sta_ssid, sizeof(sta.sta.ssid));
    strlcpy((char *)sta.sta.password, g_set.sta_pass, sizeof(sta.sta.password));
    esp_wifi_set_config(WIFI_IF_STA, &sta);
    esp_wifi_disconnect();
    if (g_set.sta_ssid[0]) esp_wifi_connect();
}

void net_init(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    s_ap = esp_netif_create_default_wifi_ap();
    s_sta = esp_netif_create_default_wifi_sta();

    // AP aadress 4.3.2.1 ja DHCP pakub DNS-iks seadet ennast
    esp_netif_ip_info_t ip = {};
    ip.ip.addr = ESP_IP4TOADDR(4, 3, 2, 1);
    ip.gw.addr = ESP_IP4TOADDR(4, 3, 2, 1);
    ip.netmask.addr = ESP_IP4TOADDR(255, 255, 255, 0);
    esp_netif_dhcps_stop(s_ap);
    esp_netif_set_ip_info(s_ap, &ip);
    esp_netif_dns_info_t dns = {};
    dns.ip.u_addr.ip4.addr = ESP_IP4TOADDR(4, 3, 2, 1);
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    esp_netif_set_dns_info(s_ap, ESP_NETIF_DNS_MAIN, &dns);
    uint8_t offer = 0x02;  // OFFER_DNS
    esp_netif_dhcps_option(s_ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer, sizeof(offer));
    esp_netif_dhcps_start(s_ap);

    wifi_init_config_t wc = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wc);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, nullptr);
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "ShockTest-%02X%02X", mac[4], mac[5]);
    s_ssid = ssid;
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    net_apply_settings();
    esp_wifi_start();
    esp_wifi_set_ps(WIFI_PS_NONE);  // energiasääst aeglustas OTA-d (~8 KB/s)
    xTaskCreatePinnedToCore(dns_task, "dns", 4096, nullptr, 3, nullptr, 0);
    http_start();
    ESP_LOGI(TAG, "pääsupunkt %s", s_ssid.c_str());
}
