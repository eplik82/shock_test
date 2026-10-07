// shock_test — Half-Sine Shock Pulse tester (Waveshare ESP32-S3-Touch-LCD-4.3 + ADXL375)
#include <stdio.h>
#include <string.h>

#include <string>

#include "adxl375.h"
#include "clock.h"
#include "crashinfo.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hardreset.h"
#include "lvgl_port.h"
#include "net.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "settings.h"
#include "shock.h"
#include "soc/rtc_cntl_reg.h"
#include "store.h"
#include "ui.h"
#include "version.h"

static const char *TAG = "main";

// Seeriakonsool: shot | tap x y | sim A TD [telg 0..2] [müra] | arm 0/1 | status | mem | dl | reboot
static void console_task(void *)
{
    std::string line;
    uint8_t ch;
    while (true) {
        int n = uart_read_bytes(UART_NUM_0, &ch, 1, portMAX_DELAY);
        if (n <= 0) continue;
        if (ch != '\n' && ch != '\r') {
            if (line.size() < 256) line += (char)ch;
            continue;
        }
        if (line.empty()) continue;
        std::string cmd = line.substr(0, line.find(' '));
        std::string arg = line.size() > cmd.size() + 1 ? line.substr(cmd.size() + 1) : "";
        line.clear();
        int a, b;
        float fa, fb, fc;
        if (cmd == "shot") {
            lvport_screenshot();
        } else if (cmd == "tap" && sscanf(arg.c_str(), "%d %d", &a, &b) == 2) {
            lvport_inject_tap(a, b);
        } else if (cmd == "sim") {
            fa = g_set.peak_g, fb = g_set.td_ms, fc = 0.3f;
            a = 2;
            sscanf(arg.c_str(), "%f %f %d %f", &fa, &fb, &a, &fc);
            adxl_simulate(fa, fb, a, fc);
            printf("sim %.1f g %.2f ms telg %d müra %.2f\n", fa, fb, a, fc);
        } else if (cmd == "arm" && sscanf(arg.c_str(), "%d", &a) == 1) {
            shock_arm(a != 0);
        } else if (cmd == "status") {
            float x, y, z;
            adxl_last_g(&x, &y, &z);
            printf("v%s andur %s rate %.1f Hz (ODR %d) overrun %lu i2cerr %lu | a=%.2f %.2f %.2f g | olek %d | kell %s | "
                   "AP %s, STA %s\n",
                   FW_VERSION, adxl_present() ? "OK" : "PUUDUB", adxl_measured_rate(), adxl_odr(),
                   (unsigned long)adxl_overruns(), (unsigned long)adxl_i2c_errors(), x, y, z, (int)shock_state(),
                   clock_fmt(clock_epoch()).c_str(), net_ap_ssid().c_str(), net_sta_ip().c_str());
            std::string c = crashinfo_text();
            if (!c.empty()) printf("viimane krahh: %s\n", c.c_str());
        } else if (cmd == "mem") {
            printf("sisemine vaba %u (min %u), PSRAM vaba %u\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                   (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        } else if (cmd == "dl") {
            // taaskäivita ROM allalaadimisrežiimi (välgutamiseks ilma nuppudeta)
            printf("allalaadimisrežiim\n");
            fflush(stdout);
            vTaskDelay(pdMS_TO_TICKS(50));
            REG_WRITE(RTC_CNTL_WDTWPROTECT_REG, 0x50D83AA1);
            REG_WRITE(RTC_CNTL_WDTCONFIG0_REG, 0);
            REG_WRITE(RTC_CNTL_WDTWPROTECT_REG, 0);
            REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
            REG_WRITE(RTC_CNTL_OPTIONS0_REG, RTC_CNTL_SW_SYS_RST);
            while (true) {}
        } else if (cmd == "reboot") {
            hard_restart();
        } else {
            printf("? %s\n", cmd.c_str());
        }
    }
}

// OTA: kinnita uus versioon, kui see töötab 20 s; teavita tagasipööramisest
static void ota_validate_task(void *)
{
    vTaskDelay(pdMS_TO_TICKS(20000));
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "uus püsivara kinnitatud");
    }
    vTaskDelete(nullptr);
}

static void check_rollback(void)
{
    const esp_partition_t *bad = esp_ota_get_last_invalid_partition();
    if (!bad) return;
    // teata ühe korra iga ebaõnnestunud pildi kohta
    esp_app_desc_t d;
    char key[40] = "";
    if (esp_ota_get_partition_description(bad, &d) == ESP_OK) snprintf(key, sizeof(key), "%.31s", d.version);
    nvs_handle_t h;
    char prev[40] = "";
    size_t l = sizeof(prev);
    if (nvs_open("ota", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_get_str(h, "bad", prev, &l);
    if (strcmp(prev, key) != 0) {
        nvs_set_str(h, "bad", key);
        nvs_commit(h);
        LvGuard g;
        ui_message(TR("Tarkvarauuendus ebaõnnestus", "Firmware update failed"),
                   TR("Uus püsivara ei käivitunud korrektselt. Taastati eelmine versioon " FW_VERSION ".",
                      "The new firmware did not start correctly. Previous version " FW_VERSION " was restored."));
    }
    nvs_close(h);
}

extern "C" void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    uart_driver_install(UART_NUM_0, 2048, 0, 0, nullptr, 0);
    uart_vfs_dev_use_driver(UART_NUM_0);
    esp_rom_printf("[kaivitus] " FW_NAME " v" FW_VERSION ", lähtestus %d\n", (int)esp_reset_reason());
    {
        std::string c = crashinfo_text();
        if (!c.empty()) esp_rom_printf("[kaivitus] eelmine krahh: %s\n", c.c_str());
    }

    xTaskCreate(console_task, "console", 6144, nullptr, 2, nullptr);  // kohe: "dl" töötab ka kui järgmine samm kinni jääb
#define STEP(x) do { esp_rom_printf("[kaivitus] %s\n", #x); x; } while (0)
    STEP(settings_load());
    STEP(clock_init());
    STEP(lvport_init());  // plaat (I2C, ekraan, puude) + LVGL
    {
        LvGuard g;
        STEP(ui_splash_show());
    }
    STEP(ui_init());
    {
        // esimesel käivitusel vormindatakse 9,9 MB andmepartitsioon (võib kesta minuteid)
        LvGuard g;
        ui_message(TR("Käivitus", "Starting"), TR("Avan andmemälu ... (esimesel korral vormindamine, kuni 2 min)", "Opening data storage ... (first time formatting, up to 2 min)"));
    }
    STEP(store_init());
    {
        LvGuard g;
        ui_message_close();
        ui_refresh();
    }
    STEP(adxl_init(g_set.odr));
    STEP(shock_init(ui_on_shot));
    STEP(net_init());
    ui_splash_done(2500);
    xTaskCreate(ota_validate_task, "otaval", 3072, nullptr, 1, nullptr);
    check_rollback();
    // pooleli seeria jätkub peatatuna (kasutaja vajutab "Valmis")
    ESP_LOGI(TAG, "käivitunud; andur %s, vaba PSRAM %u", adxl_present() ? "OK" : "PUUDUB",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
