#pragma once
// Täielik lähtestus RTC watchdogi kaudu (lähtestab ka RTC domeeni ja kõik välisseadmed).
// esp_restart() järel oli PSRAM-i kopeeritud kood rikutud -> krahhid; see lähtestus käitub nagu külmkäivitus.
[[noreturn]] void hard_restart(void);

// GPIO0 (käivitusrežiimi valik) on ka ekraani andmeliin G3. Paneeli sisend hoiab viimast taset (bus-hold):
// kui see jäi madalaks, käivitub kiip lähtestusel allalaadimisrežiimi (must ekraan). Seepärast enne iga
// lähtestust: GPIO0 lihtsaks GPIO väljundiks ja kõrgeks. Töötab ka katkestusteta kontekstis (otse registrid).
void gpio0_force_high(void);
