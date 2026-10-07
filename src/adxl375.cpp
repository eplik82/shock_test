#include "adxl375.h"

#include <math.h>
#include <string.h>

#include "board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "adxl";

#define ADXL_ADDR 0x53
#define REG_DEVID 0x00
#define REG_BW_RATE 0x2C
#define REG_POWER_CTL 0x2D
#define REG_INT_ENABLE 0x2E
#define REG_DATA_FORMAT 0x31
#define REG_DATAX0 0x32
#define REG_FIFO_CTL 0x38
#define REG_FIFO_STATUS 0x39

#define RING 16384  // 2 astme, ~5 s @3200 Hz

static i2c_master_dev_handle_t s_dev;
static bool s_present;
static int s_odr = 3200;
static AccSample *s_ring;
static volatile uint32_t s_count;
static volatile uint32_t s_overruns, s_i2c_err;
static float s_rate;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

// simulatsioon
static volatile bool s_sim_req;
static float s_sim_peak, s_sim_td, s_sim_noise;
static int s_sim_axis;
static int64_t s_sim_start_n = -1;  // valimiindeks, kust impulss algab

static esp_err_t wr(uint8_t reg, uint8_t v)
{
    uint8_t b[2] = {reg, v};
    return i2c_master_transmit(s_dev, b, 2, 20);
}

static esp_err_t rd(uint8_t reg, uint8_t *dst, size_t n)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, dst, n, 20);
}

static uint8_t rate_code(int odr)
{
    if (odr >= 3200) return 0x0F;
    if (odr >= 1600) return 0x0E;
    if (odr >= 800) return 0x0D;
    return 0x0C;  // 400 Hz
}

static inline float gauss(void)
{
    // lihtne ~normaaljaotus (12 ühtlase summa)
    float s = 0;
    for (int i = 0; i < 4; i++) s += (float)(esp_random() & 0xffff) / 65535.0f;
    return (s - 2.0f) * 1.73f;
}

static void push(AccSample a)
{
    uint32_t n = s_count;
    // simulatsiooni impulss
    if (s_sim_req) {
        s_sim_req = false;
        s_sim_start_n = n + (int64_t)(0.05f * s_odr);  // 50 ms pärast
    }
    if (s_sim_start_n >= 0) {
        float t = (float)((int64_t)n - s_sim_start_n) / (s_rate > 0 ? s_rate : s_odr) * 1000.0f;  // ms
        float g = 0;
        if (t >= 0 && t <= s_sim_td) g = s_sim_peak * sinf((float)M_PI * t / s_sim_td);
        if (t > s_sim_td * 4) s_sim_start_n = -1;
        if (s_sim_noise > 0) g += s_sim_noise * gauss();
        int16_t *ax = s_sim_axis == 0 ? &a.x : s_sim_axis == 1 ? &a.y : &a.z;
        int v = *ax + (int)lroundf(g / ADXL_LSB_G);
        if (v > 4095) v = 4095;  // 13-bit küllastus (±200 g ~ ±4096)
        if (v < -4096) v = -4096;
        *ax = (int16_t)v;
    }
    s_ring[n & (RING - 1)] = a;
    __atomic_store_n(&s_count, n + 1, __ATOMIC_RELEASE);
}

static void rate_update(void)
{
    static int64_t t0;
    static uint32_t c0;
    int64_t now = esp_timer_get_time();
    if (t0 == 0) {
        t0 = now;
        c0 = s_count;
        return;
    }
    if (now - t0 >= 2000000) {
        float r = (float)(s_count - c0) * 1e6f / (float)(now - t0);
        // libisev keskmine; esimene mõõt otse
        s_rate = s_rate == 0 ? r : s_rate * 0.8f + r * 0.2f;
        t0 = now;
        c0 = s_count;
    }
}

static void sensor_task(void *)
{
    uint8_t buf[6];
    while (true) {
        uint8_t st = 0;
        if (rd(REG_FIFO_STATUS, &st, 1) != ESP_OK) {
            s_i2c_err++;
            vTaskDelay(1);
            continue;
        }
        int n = st & 0x3F;
        if (n >= 32) s_overruns++;
        if (n == 0) {
            vTaskDelay(1);
            rate_update();
            continue;
        }
        for (int i = 0; i < n; i++) {
            if (rd(REG_DATAX0, buf, 6) != ESP_OK) {
                s_i2c_err++;
                break;
            }
            AccSample a;
            a.x = (int16_t)(buf[0] | (buf[1] << 8));
            a.y = (int16_t)(buf[2] | (buf[3] << 8));
            a.z = (int16_t)(buf[4] | (buf[5] << 8));
            push(a);
        }
        rate_update();
    }
}

// Andur puudub: genereerime valimid ajastatult (1 g Z-teljel + müra), et UI ja analüüsi saaks testida
static void fake_task(void *)
{
    int64_t next = esp_timer_get_time();
    const int64_t per = 1000000 / s_odr;
    while (true) {
        int64_t now = esp_timer_get_time();
        while (next <= now) {
            AccSample a;
            a.x = (int16_t)lroundf(0.3f * gauss());
            a.y = (int16_t)lroundf(0.3f * gauss());
            a.z = (int16_t)lroundf(1.0f / ADXL_LSB_G + 0.3f * gauss());
            push(a);
            next += per;
        }
        rate_update();
        vTaskDelay(1);
    }
}

bool adxl_init(int odr_hz)
{
    s_odr = odr_hz >= 3200 ? 3200 : odr_hz >= 1600 ? 1600 : 800;
    s_ring = (AccSample *)heap_caps_calloc(RING, sizeof(AccSample), MALLOC_CAP_SPIRAM);
    i2c_device_config_t dev = {};
    dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev.device_address = ADXL_ADDR;
    dev.scl_speed_hz = 400000;
    if (i2c_master_bus_add_device(board_i2c_bus(), &dev, &s_dev) != ESP_OK) return false;
    uint8_t id = 0;
    if (rd(REG_DEVID, &id, 1) == ESP_OK && id == 0xE5) {
        wr(REG_POWER_CTL, 0x00);            // ooterežiim seadistamise ajaks
        wr(REG_DATA_FORMAT, 0x0B);          // ADXL375: D3, D1, D0 peavad olema 1
        wr(REG_BW_RATE, rate_code(s_odr));  // tavaline võimsus
        wr(REG_INT_ENABLE, 0x00);
        wr(REG_FIFO_CTL, 0x80 | 16);        // stream, watermark 16
        wr(REG_POWER_CTL, 0x08);            // mõõtmine
        s_present = true;
        ESP_LOGI(TAG, "ADXL375 leitud, ODR %d Hz", s_odr);
        xTaskCreatePinnedToCore(sensor_task, "adxl", 4096, nullptr, configMAX_PRIORITIES - 3, nullptr, 0);
    } else {
        ESP_LOGW(TAG, "ADXL375 ei vasta (DEVID 0x%02x) - simulatsioonirežiim", id);
        s_present = false;
        xTaskCreatePinnedToCore(fake_task, "adxlsim", 4096, nullptr, configMAX_PRIORITIES - 3, nullptr, 0);
    }
    return s_present;
}

bool adxl_present(void) { return s_present; }
int adxl_odr(void) { return s_odr; }
uint32_t adxl_count(void) { return __atomic_load_n(&s_count, __ATOMIC_ACQUIRE); }
size_t adxl_ring_size(void) { return RING; }
uint32_t adxl_overruns(void) { return s_overruns; }
uint32_t adxl_i2c_errors(void) { return s_i2c_err; }
float adxl_measured_rate(void) { return s_rate > 0 ? s_rate : (float)s_odr; }

size_t adxl_copy(uint32_t from, AccSample *dst, size_t n)
{
    uint32_t c = adxl_count();
    if (from + n > c) n = c > from ? c - from : 0;
    if (c - from > RING - 64) return 0;  // juba üle kirjutatud
    for (size_t i = 0; i < n; i++) dst[i] = s_ring[(from + i) & (RING - 1)];
    return n;
}

void adxl_simulate(float peak_g, float td_ms, int axis, float noise_g)
{
    s_sim_peak = peak_g;
    s_sim_td = td_ms;
    s_sim_axis = axis < 0 ? 2 : axis > 2 ? 2 : axis;
    s_sim_noise = noise_g;
    s_sim_req = true;
}

void adxl_last_g(float *x, float *y, float *z)
{
    uint32_t c = adxl_count();
    if (c == 0) {
        *x = *y = *z = 0;
        return;
    }
    AccSample a = s_ring[(c - 1) & (RING - 1)];
    *x = a.x * ADXL_LSB_G;
    *y = a.y * ADXL_LSB_G;
    *z = a.z * ADXL_LSB_G;
}
