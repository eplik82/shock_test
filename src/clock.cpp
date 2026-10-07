#include "clock.h"

#include <sys/time.h>
#include <time.h>

#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"

static bool s_valid;
static int s_tz;
static uint32_t s_boot;

void clock_init(void)
{
    s_boot = esp_random();
    nvs_handle_t h;
    if (nvs_open("clock", NVS_READONLY, &h) == ESP_OK) {
        int32_t tz = 0;
        if (nvs_get_i32(h, "tz", &tz) == ESP_OK) s_tz = tz;
        nvs_close(h);
    }
}

bool clock_valid(void) { return s_valid; }
int clock_tz_min(void) { return s_tz; }
uint32_t clock_boot_id(void) { return s_boot; }
uint32_t clock_uptime_s(void) { return (uint32_t)(esp_timer_get_time() / 1000000); }

void clock_set(int64_t epoch_ms, int tz_min)
{
    struct timeval tv = {(time_t)(epoch_ms / 1000), (suseconds_t)((epoch_ms % 1000) * 1000)};
    settimeofday(&tv, nullptr);
    s_valid = true;
    if (tz_min != s_tz) {
        s_tz = tz_min;
        nvs_handle_t h;
        if (nvs_open("clock", NVS_READWRITE, &h) == ESP_OK) {
            nvs_set_i32(h, "tz", tz_min);
            nvs_commit(h);
            nvs_close(h);
        }
    }
}

int64_t clock_epoch(void)
{
    if (!s_valid) return 0;
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec;
}

std::string clock_fmt(int64_t epoch)
{
    if (epoch <= 0) return "—";
    time_t t = (time_t)(epoch + s_tz * 60);
    struct tm tm;
    gmtime_r(&t, &tm);
    char b[32];
    strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &tm);
    return b;
}
