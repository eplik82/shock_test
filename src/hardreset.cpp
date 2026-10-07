#include "hardreset.h"

#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_private/rtc_clk.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "hal/wdt_hal.h"
#include "soc/gpio_reg.h"
#include "soc/io_mux_reg.h"
#include "soc/rtc.h"
#include "lvgl_port.h"

extern void board_shutdown_display(void);

void IRAM_ATTR gpio0_force_high(void)
{
    REG_WRITE(GPIO_FUNC0_OUT_SEL_CFG_REG, 0x100);  // lihtne GPIO väljund (mitte LCD signaal)
    REG_WRITE(GPIO_OUT_W1TS_REG, BIT(0));
    REG_WRITE(GPIO_ENABLE_W1TS_REG, BIT(0));
    esp_rom_delay_us(2000);
}

[[noreturn]] void hard_restart(void)
{
    lvport_lock(1000);          // LVGL ei tohi samal ajal ekraanile kirjutada
    board_shutdown_display();  // ekraani DMA seisma enne lähtestust
    gpio0_force_high();        // bus-hold jätab GPIO0 kõrgeks -> tavaline käivitus, mitte allalaadimisrežiim
    esp_rom_printf("[hard_restart]\n");
    wdt_hal_context_t rtc = {};
    wdt_hal_init(&rtc, WDT_RWDT, 0, false);
    uint32_t ticks = (uint32_t)(50ULL * rtc_clk_slow_freq_get_hz() / 1000ULL);  // 50 ms
    wdt_hal_write_protect_disable(&rtc);
    wdt_hal_config_stage(&rtc, WDT_STAGE0, ticks, WDT_STAGE_ACTION_RESET_RTC);
    wdt_hal_config_stage(&rtc, WDT_STAGE1, 0, WDT_STAGE_ACTION_OFF);
    wdt_hal_config_stage(&rtc, WDT_STAGE2, 0, WDT_STAGE_ACTION_OFF);
    wdt_hal_config_stage(&rtc, WDT_STAGE3, 0, WDT_STAGE_ACTION_OFF);
    wdt_hal_set_flashboot_en(&rtc, false);
    wdt_hal_enable(&rtc);
    wdt_hal_write_protect_enable(&rtc);
    portDISABLE_INTERRUPTS();
    while (true) {}
}
