#include "board.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_rom_sys.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board";

// --- Viigud (Waveshare ESP32-S3-Touch-LCD-4.3B) ---
#define PIN_I2C_SDA GPIO_NUM_8
#define PIN_I2C_SCL GPIO_NUM_9
#define PIN_TOUCH_INT GPIO_NUM_4

#define PIN_PCLK GPIO_NUM_7
#define PIN_HSYNC GPIO_NUM_46
#define PIN_VSYNC GPIO_NUM_3
#define PIN_DE GPIO_NUM_5

// CH422G "käsuaadressid" (iga register on eraldi I2C aadress)
#define CH422G_ADDR_MODE 0x24
#define CH422G_ADDR_OUT 0x38
// EXIO bitid
#define EXIO_TP_RST (1 << 1)
#define EXIO_BL (1 << 2)
#define EXIO_LCD_RST (1 << 3)
#define EXIO_SD_CS (1 << 4)
#define EXIO_USB_SEL (1 << 5)

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_ch_mode, s_ch_out;
// taustvalgus kohe sees (hilisem sisselülitus CH422G kaudu langes kokku lähtestusega)
static uint8_t s_exio = EXIO_LCD_RST | EXIO_SD_CS | EXIO_USB_SEL | EXIO_BL;
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_touch_handle_t s_touch;
static board_vsync_cb_t s_vsync_cb;
static void *s_vsync_ctx;

static void exio_write(void)
{
    i2c_master_transmit(s_ch_out, &s_exio, 1, 100);
}

static void exio_set(uint8_t mask, bool on)
{
    if (on)
        s_exio |= mask;
    else
        s_exio &= ~mask;
    exio_write();
}

void board_backlight(bool on) { exio_set(EXIO_BL, on); }

static bool IRAM_ATTR on_vsync(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t *, void *)
{
    return s_vsync_cb ? s_vsync_cb(s_vsync_ctx) : false;
}

static bool s_touch_ok;
static char s_diag[256];
#define DIAG(...) do { size_t l_ = strlen(s_diag); snprintf(s_diag + l_, sizeof(s_diag) - l_, __VA_ARGS__); } while (0)
const char *board_i2c_diag(void) { return s_diag; }

// I2C diagnostika: liinide tasemed, kinni jäänud siini vabastamine (9 kella + STOP)
static void i2c_unstick(void)
{
    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << PIN_I2C_SDA) | (1ULL << PIN_I2C_SCL);
    io.mode = GPIO_MODE_INPUT_OUTPUT_OD;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io);
    gpio_set_level(PIN_I2C_SDA, 1);
    gpio_set_level(PIN_I2C_SCL, 1);
    esp_rom_delay_us(50);
    int sda = gpio_get_level(PIN_I2C_SDA), scl = gpio_get_level(PIN_I2C_SCL);
    esp_rom_printf("[i2c] tasemed enne: SDA=%d SCL=%d\n", sda, scl);
    DIAG("SDA=%d SCL=%d", sda, scl);
    if (!sda && scl) {
        for (int i = 0; i < 9 && !gpio_get_level(PIN_I2C_SDA); i++) {
            gpio_set_level(PIN_I2C_SCL, 0);
            esp_rom_delay_us(10);
            gpio_set_level(PIN_I2C_SCL, 1);
            esp_rom_delay_us(10);
        }
        gpio_set_level(PIN_I2C_SDA, 0);
        esp_rom_delay_us(10);
        gpio_set_level(PIN_I2C_SDA, 1);  // STOP
        esp_rom_delay_us(10);
        esp_rom_printf("[i2c] pärast vabastamist: SDA=%d SCL=%d\n", gpio_get_level(PIN_I2C_SDA), gpio_get_level(PIN_I2C_SCL));
        DIAG(" (vabastus: SDA=%d)", gpio_get_level(PIN_I2C_SDA));
    }
    gpio_reset_pin(PIN_I2C_SDA);
    gpio_reset_pin(PIN_I2C_SCL);
}

static void i2c_scan(void)
{
    const uint8_t addrs[] = {0x24, 0x38, 0x5D, 0x14, 0x53};
    const char *names[] = {"CH422G mode", "CH422G out", "GT911", "GT911 alt", "ADXL375"};
    for (int i = 0; i < 5; i++) {
        esp_err_t e = i2c_master_probe(s_bus, addrs[i], 50);
        esp_rom_printf("[i2c] 0x%02x %-12s %s\n", addrs[i], names[i], e == ESP_OK ? "OK" : esp_err_to_name(e));
        DIAG(", %s %s", names[i], e == ESP_OK ? "OK" : "-");
        if (e == ESP_OK && (addrs[i] == 0x5D || addrs[i] == 0x14)) s_touch_ok = true;
    }
}

static void init_i2c(void)
{
    i2c_unstick();
    i2c_master_bus_config_t bus = {};
    bus.i2c_port = I2C_NUM_0;
    bus.sda_io_num = PIN_I2C_SDA;
    bus.scl_io_num = PIN_I2C_SCL;
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &s_bus));

    i2c_device_config_t dev = {};
    dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev.scl_speed_hz = 400000;
    dev.device_address = CH422G_ADDR_MODE;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &dev, &s_ch_mode));
    dev.device_address = CH422G_ADDR_OUT;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &dev, &s_ch_out));

    i2c_scan();
    uint8_t mode = 0x01;  // IO0-7 väljunditeks
    if (i2c_master_transmit(s_ch_mode, &mode, 1, 100) != ESP_OK)
        ESP_LOGE(TAG, "CH422G ei vasta");
}

static void init_touch(void)
{

    // GT911 reset: INT madalaks reseti ajal -> I2C aadress 0x5D
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << PIN_TOUCH_INT;
    io.mode = GPIO_MODE_OUTPUT;
    gpio_config(&io);
    exio_set(EXIO_TP_RST, false);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_TOUCH_INT, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    exio_set(EXIO_TP_RST, true);
    vTaskDelay(pdMS_TO_TICKS(60));
    io.mode = GPIO_MODE_INPUT;
    gpio_config(&io);
    vTaskDelay(pdMS_TO_TICKS(50));
    // esp_lcd I2C ootab lõputult: kui GT911 ei vasta, jätame puute vahele (muidu jääb käivitus kinni)
    esp_err_t pe = i2c_master_probe(s_bus, 0x5D, 50);
    esp_rom_printf("[i2c] GT911 pärast reset'i: %s\n", pe == ESP_OK ? "OK" : esp_err_to_name(pe));
    if (pe != ESP_OK) {
        ESP_LOGE(TAG, "GT911 ei vasta - puude välja lülitatud");
        s_touch = nullptr;
        return;
    }

    esp_lcd_panel_io_handle_t tp_io;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_cfg.scl_speed_hz = 400000;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(s_bus, &io_cfg, &tp_io));

    esp_lcd_touch_config_t cfg = {};
    cfg.x_max = LCD_W;
    cfg.y_max = LCD_H;
    cfg.rst_gpio_num = GPIO_NUM_NC;
    cfg.int_gpio_num = GPIO_NUM_NC;
    if (esp_lcd_touch_new_i2c_gt911(tp_io, &cfg, &s_touch) != ESP_OK) {
        ESP_LOGE(TAG, "GT911 init ebaõnnestus");
        s_touch = nullptr;
    }
}

static void init_panel(void)
{
    esp_lcd_rgb_panel_config_t cfg = {};
    cfg.clk_src = LCD_CLK_SRC_DEFAULT;
    cfg.timings.pclk_hz = 16 * 1000 * 1000;  // paneel kaotab madalamal (<=15 MHz) sünkroniseerimise (pilt nihkub)
    cfg.timings.h_res = LCD_W;
    cfg.timings.v_res = LCD_H;
    cfg.timings.hsync_pulse_width = 4;
    cfg.timings.hsync_back_porch = 8;
    cfg.timings.hsync_front_porch = 8;
    cfg.timings.vsync_pulse_width = 4;
    cfg.timings.vsync_back_porch = 8;
    cfg.timings.vsync_front_porch = 8;
    cfg.timings.flags.pclk_active_neg = 1;
    cfg.data_width = 16;
    cfg.bits_per_pixel = 16;
    cfg.num_fbs = LCD_DOUBLE_FB ? 2 : 1;
    // Põrkepuhvrid: DMA loeb pilti sisemisest SRAM-ist, mitte PSRAM-ist -> kerimisel ei värele.
    // (Varasemad krahhid tulid pinu ületäitumisest, mitte põrkepuhvritest.)
    cfg.bounce_buffer_size_px = LCD_W * 10;  // Waveshare väärtus; suurem (16-20 rida) täidab eellaadimisega D-vahemälu -> katkestus aeglustub
    cfg.dma_burst_size = 64;
    cfg.hsync_gpio_num = PIN_HSYNC;
    cfg.vsync_gpio_num = PIN_VSYNC;
    cfg.de_gpio_num = PIN_DE;
    cfg.pclk_gpio_num = PIN_PCLK;
    cfg.disp_gpio_num = GPIO_NUM_NC;
    // D0..D15 = B3..B7, G2..G7, R3..R7
    const gpio_num_t data[16] = {
        GPIO_NUM_14, GPIO_NUM_38, GPIO_NUM_18, GPIO_NUM_17, GPIO_NUM_10,             // B
        GPIO_NUM_39, GPIO_NUM_0,  GPIO_NUM_45, GPIO_NUM_48, GPIO_NUM_47, GPIO_NUM_21, // G
        GPIO_NUM_1,  GPIO_NUM_2,  GPIO_NUM_42, GPIO_NUM_41, GPIO_NUM_40,             // R
    };
    for (int i = 0; i < 16; i++) cfg.data_gpio_nums[i] = data[i];
    cfg.flags.fb_in_psram = 1;

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&cfg, &s_panel));
    esp_lcd_rgb_panel_event_callbacks_t cbs = {};
    cbs.on_vsync = on_vsync;
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(s_panel, &cbs, nullptr));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
}

// esp_restart() ei lähtesta LCD_CAM välisseadet -> enne taaskäivitust peatame ekraani ja selle DMA korrektselt
void board_shutdown_display(void)
{
    // NB: siin ei tohi I2C-d kasutada (kutsutakse ka katkestusteta kontekstist) -> ainult ekraani DMA seisma
    if (s_panel) {
        esp_lcd_panel_del(s_panel);
        s_panel = nullptr;
    }
}

void board_init(board_vsync_cb_t cb, void *ctx)
{
    s_vsync_cb = cb;
    s_vsync_ctx = ctx;
    init_i2c();
    exio_write();  // LCD reset kõrgeks, taustvalgus veel väljas
    init_touch();
    init_panel();
    esp_register_shutdown_handler(board_shutdown_display);
    ESP_LOGI(TAG, "plaat valmis");
}

esp_lcd_panel_handle_t board_panel(void) { return s_panel; }
i2c_master_bus_handle_t board_i2c_bus(void) { return s_bus; }

void board_get_framebuffers(void **fb0, void **fb1)
{
#if LCD_DOUBLE_FB
    esp_lcd_rgb_panel_get_frame_buffer(s_panel, 2, fb0, fb1);
#else
    esp_lcd_rgb_panel_get_frame_buffer(s_panel, 1, fb0);
    if (fb1) *fb1 = nullptr;
#endif
}

bool board_touch_read(int *x, int *y)
{
    if (!s_touch) return false;
    if (esp_lcd_touch_read_data(s_touch) != ESP_OK) return false;
    esp_lcd_touch_point_data_t pt[1];
    uint8_t cnt = 0;
    esp_lcd_touch_get_data(s_touch, pt, &cnt, 1);
    if (cnt == 0) return false;
    *x = pt[0].x;
    *y = pt[0].y;
    return true;
}
