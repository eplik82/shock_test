#include "lvgl_port.h"

#include <string.h>

#include "board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "lvport";

static SemaphoreHandle_t s_mutex;
static SemaphoreHandle_t s_vsync;
static lv_display_t *s_disp;
static void *s_fb[2];
static volatile const uint16_t *s_shown_fb;

// --- süstitud sisendi järjekord ---
struct InjPoint {
    int16_t x, y;
    bool pressed;
};
static InjPoint s_inj[128];
static volatile int s_inj_head, s_inj_tail;
static portMUX_TYPE s_inj_lock = portMUX_INITIALIZER_UNLOCKED;

static void inj_push(int x, int y, bool pressed)
{
    taskENTER_CRITICAL(&s_inj_lock);
    int next = (s_inj_head + 1) % 128;
    if (next != s_inj_tail) {
        s_inj[s_inj_head] = {(int16_t)x, (int16_t)y, pressed};
        s_inj_head = next;
    }
    taskEXIT_CRITICAL(&s_inj_lock);
}

static bool inj_pop(InjPoint *p)
{
    bool ok = false;
    taskENTER_CRITICAL(&s_inj_lock);
    if (s_inj_tail != s_inj_head) {
        *p = s_inj[s_inj_tail];
        s_inj_tail = (s_inj_tail + 1) % 128;
        ok = true;
    }
    taskEXIT_CRITICAL(&s_inj_lock);
    return ok;
}

void lvport_inject_tap(int x, int y)
{
    for (int i = 0; i < 3; i++) inj_push(x, y, true);
    inj_push(x, y, false);
}

void lvport_inject_drag(int x1, int y1, int x2, int y2, int steps)
{
    if (steps < 2) steps = 2;
    for (int i = 0; i <= steps; i++)
        inj_push(x1 + (x2 - x1) * i / steps, y1 + (y2 - y1) * i / steps, true);
    inj_push(x2, y2, true);
    inj_push(x2, y2, false);
}

static bool on_vsync(void *)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_vsync, &woken);
    return woken == pdTRUE;
}

// Joonistamine sisemises SRAM-is (PARTIAL) + topeltpuhver PSRAM-is:
//  - LVGL joonistab SRAM-ribadesse -> PSRAM-i ei loeta/kirjutata piksli kaupa (DIRECT-režiim näljutas
//    põrkepuhvri katkestust -> katkestuse watchdog)
//  - ribad kopeeritakse NÄHTAMATUSSE puhvrisse; kaadri lõpus vahetus vsync'il -> poolikut kaadrit pole näha
//  - pärast vahetust kopeeritakse muutunud alad uude tagapuhvrisse, et see oleks ajakohane
static int s_back = 1;  // puhver, kuhu joonistatakse
#define MAX_DIRTY 24
static lv_area_t s_dirty[MAX_DIRTY];
static int s_ndirty;
static bool s_dirty_all;

static void copy_area(uint16_t *dst, const uint16_t *src, const lv_area_t *a)
{
    const int32_t w = a->x2 - a->x1 + 1;
    for (int32_t y = a->y1; y <= a->y2; y++) {
        size_t o = (size_t)y * LCD_W + a->x1;
        memcpy(dst + o, src + o, w * 2);
    }
}

#if !LCD_DOUBLE_FB
// üks kaadripuhver: ribad kopeeritakse otse kuvatavasse puhvrisse.
// Iga kaadri esimene riba ootab vsync'i -> joonistamine liigub ülalt alla koos ekraani lugemisega (vähem rebenemist).
static bool s_in_frame;
static volatile int64_t s_last_touch_us;

bool lvport_user_active(void)
{
    return esp_timer_get_time() - s_last_touch_us < 1500000;
}

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    if (!s_in_frame) {
        s_in_frame = true;
        xSemaphoreTake(s_vsync, 0);
        xSemaphoreTake(s_vsync, pdMS_TO_TICKS(40));
    }
    if (lv_display_flush_is_last(d)) s_in_frame = false;
    uint16_t *fb = (uint16_t *)s_fb[0];
    const int32_t w = a->x2 - a->x1 + 1;
    const uint16_t *src = (const uint16_t *)px;
    for (int32_t y = a->y1; y <= a->y2; y++) {
        memcpy(fb + (size_t)y * LCD_W + a->x1, src, w * 2);
        src += w;
    }
    lv_display_flush_ready(d);
}
#else
static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    uint16_t *back = (uint16_t *)s_fb[s_back];
    const int32_t w = a->x2 - a->x1 + 1;
    const uint16_t *src = (const uint16_t *)px;
    for (int32_t y = a->y1; y <= a->y2; y++) {
        memcpy(back + (size_t)y * LCD_W + a->x1, src, w * 2);
        src += w;
    }
    if (!s_dirty_all) {
        if (s_ndirty < MAX_DIRTY) s_dirty[s_ndirty++] = *a;
        else s_dirty_all = true;
    }
    if (lv_display_flush_is_last(d)) {
        // vaheta puhvrid: draw_bitmap puhvri enda aadressiga = vahetus (jõustub järgmise kaadri algul)
        esp_lcd_panel_draw_bitmap(board_panel(), 0, 0, LCD_W, LCD_H, back);
        xSemaphoreTake(s_vsync, 0);
        xSemaphoreTake(s_vsync, pdMS_TO_TICKS(60));
        s_shown_fb = back;
        int front = s_back;
        s_back ^= 1;
        uint16_t *nb = (uint16_t *)s_fb[s_back];
        const uint16_t *fr = (const uint16_t *)s_fb[front];
        if (s_dirty_all) {
            memcpy(nb, fr, LCD_W * LCD_H * 2);
        } else {
            for (int i = 0; i < s_ndirty; i++) copy_area(nb, fr, &s_dirty[i]);
        }
        s_ndirty = 0;
        s_dirty_all = false;
    }
    lv_display_flush_ready(d);
}
#endif

static void touch_read_cb(lv_indev_t *, lv_indev_data_t *data)
{
    static InjPoint last = {0, 0, false};
    InjPoint p;
    if (inj_pop(&p)) {
        last = p;
        data->point.x = p.x;
        data->point.y = p.y;
        data->state = p.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        if (s_inj_tail != s_inj_head) data->continue_reading = false;
        return;
    }
    int x, y;
    if (board_touch_read(&x, &y)) {
#if !LCD_DOUBLE_FB
        s_last_touch_us = esp_timer_get_time();
#endif
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static uint32_t tick_cb(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

bool lvport_lock(int timeout_ms)
{
    return xSemaphoreTakeRecursive(s_mutex, timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void lvport_unlock(void) { xSemaphoreGiveRecursive(s_mutex); }

static void lvgl_task(void *)
{
    int n = 0;
    while (true) {
        if (n < 5) esp_rom_printf("[lvgl] kaader %d\n", n);
        n++;
        lvport_lock();
        uint32_t wait = lv_timer_handler();
        lvport_unlock();
        if (wait > 20) wait = 20;
        if (wait < 2) wait = 2;
        vTaskDelay(pdMS_TO_TICKS(wait));
    }
}

void lvport_init(void)
{
    s_mutex = xSemaphoreCreateRecursiveMutex();
    s_vsync = xSemaphoreCreateBinary();
    // Paneel (ja selle DMA/põrkepuhvri katkestused) luuakse tuumal 1: tuumal 0 töötavad WiFi, TLS ja
    // piltide dekodeerimine, mis keelavad katkestusi -> põrkepuhver jäi hiljaks ja pilt hüppas iga pildi laadimisel.
    static SemaphoreHandle_t done;
    done = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(
        [](void *) {
            board_init(on_vsync, nullptr);
            xSemaphoreGive(done);
            vTaskDelete(nullptr);
        },
        "lcd_init", 6144, nullptr, 5, nullptr, 1);
    xSemaphoreTake(done, portMAX_DELAY);
    vSemaphoreDelete(done);
    board_get_framebuffers(&s_fb[0], &s_fb[1]);
    memset(s_fb[0], 0xff, LCD_W * LCD_H * 2);
    if (s_fb[1]) memset(s_fb[1], 0xff, LCD_W * LCD_H * 2);
    s_shown_fb = (const uint16_t *)s_fb[0];

    lv_init();
    lv_tick_set_cb(tick_cb);
    s_disp = lv_display_create(LCD_W, LCD_H);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    const size_t draw_sz = LCD_W * 8 * 2;  // 8 rida sisemises SRAM-is (põrkepuhvrid vajavad 64 KB)
    void *draw_buf = heap_caps_malloc(draw_sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!draw_buf) draw_buf = heap_caps_malloc(draw_sz, MALLOC_CAP_SPIRAM);
    lv_display_set_buffers(s_disp, draw_buf, nullptr, draw_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_disp, flush_cb);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    lv_indev_set_display(indev, s_disp);

    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 24576, nullptr, 4, nullptr, 1);
    ESP_LOGI(TAG, "LVGL valmis");
}

// --- ekraanipilt: RLE (u16 kordus, u16 piksel) -> base64 read ---
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

const uint16_t *lvport_front_buffer(void)
{
    return s_shown_fb ? (const uint16_t *)s_shown_fb : (const uint16_t *)s_fb[0];
}

uint8_t *lvport_screenshot_rle(size_t *len)
{
    size_t n = LCD_W * LCD_H;
    uint16_t *copy = (uint16_t *)heap_caps_malloc(n * 2, MALLOC_CAP_SPIRAM);
    if (!copy) return nullptr;
    lvport_lock();
    const uint16_t *src = (const uint16_t *)s_shown_fb;
    if (!src) src = (const uint16_t *)s_fb[0];
    memcpy(copy, src, n * 2);
    lvport_unlock();
    uint8_t *rle = (uint8_t *)heap_caps_malloc(n * 4, MALLOC_CAP_SPIRAM);
    if (!rle) {
        free(copy);
        return nullptr;
    }
    size_t rl = 0;
    for (size_t i = 0; i < n;) {
        uint16_t px = copy[i];
        size_t j = i + 1;
        while (j < n && copy[j] == px && j - i < 65535) j++;
        uint16_t cnt = j - i;
        rle[rl++] = cnt & 0xff;
        rle[rl++] = cnt >> 8;
        rle[rl++] = px & 0xff;
        rle[rl++] = px >> 8;
        i = j;
    }
    free(copy);
    *len = rl;
    return rle;
}

void lvport_screenshot(void)
{
    // kopeeri kaader luku all, et pilt oleks terviklik
    size_t n = LCD_W * LCD_H;
    uint16_t *copy = (uint16_t *)heap_caps_malloc(n * 2, MALLOC_CAP_SPIRAM);
    if (!copy) {
        printf("@SERR nomem\n");
        return;
    }
    // lukk kogu väljastuse ajaks, et LVGL logi ei seguneks pildiandmetega
    lvport_lock();
    esp_log_level_set("*", ESP_LOG_NONE);  // teiste lõimede logi ei tohi pildiandmetega seguneda
    const uint16_t *src = (const uint16_t *)s_shown_fb;
    if (!src) src = (const uint16_t *)s_fb[0];
    memcpy(copy, src, n * 2);

    uint8_t *rle = (uint8_t *)heap_caps_malloc(n * 4, MALLOC_CAP_SPIRAM);
    if (!rle) {
        free(copy);
        lvport_unlock();
        printf("@SERR nomem\n");
        return;
    }
    size_t rl = 0;
    for (size_t i = 0; i < n;) {
        uint16_t px = copy[i];
        size_t j = i + 1;
        while (j < n && copy[j] == px && j - i < 65535) j++;
        uint16_t cnt = j - i;
        rle[rl++] = cnt & 0xff;
        rle[rl++] = cnt >> 8;
        rle[rl++] = px & 0xff;
        rle[rl++] = px >> 8;
        i = j;
    }
    free(copy);
    printf("\n@SBEGIN %d %d %u\n", LCD_W, LCD_H, (unsigned)rl);
    char line[2 + 96 + 2];
    for (size_t i = 0; i < rl; i += 72) {
        size_t m = rl - i < 72 ? rl - i : 72;
        int o = 0;
        line[o++] = '@';
        line[o++] = 'S';
        for (size_t k = 0; k < m; k += 3) {
            uint32_t v = rle[i + k] << 16;
            if (k + 1 < m) v |= rle[i + k + 1] << 8;
            if (k + 2 < m) v |= rle[i + k + 2];
            line[o++] = B64[(v >> 18) & 63];
            line[o++] = B64[(v >> 12) & 63];
            line[o++] = k + 1 < m ? B64[(v >> 6) & 63] : '=';
            line[o++] = k + 2 < m ? B64[v & 63] : '=';
        }
        line[o++] = '\n';
        line[o] = 0;
        fputs(line, stdout);
    }
    printf("@SEND\n");
    fflush(stdout);
    free(rle);
    esp_log_level_set("*", ESP_LOG_INFO);
    lvport_unlock();
}
