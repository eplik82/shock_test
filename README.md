# shock_test

Half-Sine Shock Pulse tester: Waveshare **ESP32-S3-Touch-LCD-4.3** + **ADXL375** (I2C 0x53).
Lähteülesanne: `LÄHTEÜLESANNE.md`. Standard: MIL-STD-810H Method 516.8 (https://cvgstrategy.com/wp-content/uploads/2019/08/MIL-STD-810H-Method-516.8-Shock.pdf).

ESP-IDF 5.5.5 + LVGL 9.2 (PlatformIO, `~/.espvenv/bin/pio`). Püsivara versioon: `src/version.h`.

## Struktuur

| Fail | Sisu |
|---|---|
| `src/adxl375.*` | andur: FIFO stream, ringpuhver (PSRAM), mõõdetud valimisagedus, simulatsioon |
| `src/shock.*` | käivituslävi, eel/järelpuhver, analüüs (tipp, TD 10 % punktidest, ΔV, kuju koridor ±0,2·A/±0,1·TD) |
| `src/store.*` | seeriad ja löögid LittleFS-is (`/lfs/tNNNN/`), kella tagantjärele dateerimine |
| `src/report.*`, `src/pdf.*` | PDF (seadmes genereeritud) ja CSV (eesti Excel: `;` ja koma) |
| `src/net.*`, `src/web/index.html` | WiFi pääsupunkt `ShockTest-XXXX` (4.3.2.1), captive DNS, portaal, OTA |
| `src/ui.*`, `src/fonts.*` | LVGL vaated: test, seaded, dialoogid; DejaVu alamhulk (eesti tähed) |
| `src/settings.*` | NVS seaded, eelseadistused (MIL-STD-810H, UN38.3 T.4) |
| `src/board.*`, `src/lvgl_port.*`, `src/hardreset.*`, `src/crashinfo.*` | plaat (browser-projektist, ekraani töötav seadistus) + I2C diagnostika |

## Ehitamine

```bash
cd /mnt/c/Claude/shock_test
~/.espvenv/bin/pio run          # väljund: ~/.cache/shock_test/build/shock/
```

## Püsivara uuendamine

Kasutaja: laadi `firmware.bin` alla [Releases](https://github.com/eplik82/shock_test/releases) lehelt,
ühenda telefon seadme WiFi-ga (`ShockTest-XXXX`, parool on ekraani kollasel real), ava portaal → „Püsivara“ →
vali fail → „Laadi üles“. Kui uus versioon ei kinnitu 20 s jooksul, taastab alglaadur eelmise.
Androidi „Logi võrku sisse“ aken (WebView) ei toeta failivalijat → captive-aknas on ülal riba nupuga „Ava Chrome'is“
(`intent://4.3.2.1/#Intent;scheme=http;end`); varuvariant on nupp „Ava Chrome'is“ või http://4.3.2.1 käsitsi.
Üleslaadimine käib 32 KB tükkidena (`/api/otachunk?off=&total=`): kinni jäänud tükki korratakse, protsent = seadmesse
kirjutatud osa. Ühe pika POST-iga jäi telefonist saatmine ~60 % juures seisma. Testitud Samsung S24+: ~150 s, ~10 KB/s.

Väljaanne: `git tag vX.Y.Z && git push origin vX.Y.Z` → GitHub Actions ehitab ja avaldab release'i.

WiFi on **ainult pääsupunkt** (4.3.2.1). Klientvõrk on välja lülitatud: AP+STA jagas raadioaega ja portaal jäi valgeks.

## USB välgutus (partitsioonitabeli/alglaaduri muutus)

```bat
C:\Claude\esp32-browser\.winvenv\Scripts\python.exe C:\Claude\shock_test\tools\flash_usb.py COM4 300
```
Kopeeri enne `~/.cache/shock_test/build/shock/{bootloader,partitions,ota_data_initial,firmware}.bin` → `bin\`.
Skript saadab seeriakäsu `dl` (allalaadimisrežiim ilma nuppudeta); kui plaat ei vasta: BOOT all + RESET.
**Pärast USB-välgutust käivitub plaat alles toite välja/sisse lülitamisel (kaabel välja ja tagasi).**

## Arendus ja testimine

Arendaja käsud töötavad pääsupunkti kaudu (IP 4.3.2.1); seeriapordis on `[net]` read (DHCP, DNS, HTTP) portaali silumiseks.

| Käsk | Mis |
|---|---|
| `curl http://IP/api/status` | olek, valimisagedus, I2C diagnostika |
| `curl http://IP/dev/log` | portaali sündmuste logi (DHCP, DNS, HTTP) |
| `curl http://IP/dev/tasks`, `/dev/wifi` | protsessoriaeg, WiFi olek (RSSI, ribalaius) |
| `tools/shot.py pilt.png` | ekraanipilt |
| `curl "http://IP/dev/tap?x=100&y=453"` | puudutus |
| `curl "http://IP/dev/sim?a10=500&td100=1100&axis=2"` | simuleeritud löök 50,0 g / 11,00 ms Z-teljel |
| seeria (COM4, 921600): `status`, `sim A TD [telg] [müra]`, `arm 1`, `shot`, `dl`, `reboot` | `tools\ser.py COM4 4 status` |

Ilma andurita töötab seade simulatsioonirežiimis (1 g Z-teljel + müra); `sim` lisab impulsi.

## Teadaolevad probleemid

- **ADXL375 ühendus (2026-10-07, lahendatud):** toide oli 3Vo viigus, anduriga hoiti I2C SDA liini madalal (SDA=0 juba enne I2C käivitust) →
  CH422G, GT911 ja ADXL375 ei vastanud. Ilma andurita on siin korras. Kontrolli anduri juhtmete järjekorda
  (3V3/GND/SDA/SCL). Käivituse diagnostika: `/api/status` väli `i2c`.
- ODR 3200 Hz ei jõua I2C kaudu (2535 Hz, ületäitumised); vaikimisi 1600 Hz (stabiilne). Muutmine: seaded või `/dev/odr?hz=1600`.
- Kordusstart (`i2c_master_transmit_receive`) 400 kHz juures ebaõnnestub selle anduriga → draiver kasutab eraldi tehinguid. Katsetus: `/dev/i2c?addr=83&reg=0&n=1&sep=1&hz=400000`.
- Adafruit ADXL375 toide **Vin** viiku (mitte 3Vo).
- Seeriapordi (CH343) avamine/sulgemine võib plaadi lähtestada; eelista WiFi-t (`/api/status`, `tools/shot.py`).
- Taustvalgus lülitatakse sisse kohe käivitusel (hilisem sisselülitus CH422G kaudu langes kokku lähtestustsükliga).
- Captive portaal: DHCP peab pakkuma DNS-i (valik enne aadressi), muidu Android „ühendatud ilma internetita“.
  Pärast portaali lehe laadimist vastatakse kliendi internetikontrollidele 204/Success, et telefon jääks võrku.
- Pärast USB-välgutust võib plaat jääda vaikseks → kaabel välja ja tagasi.
