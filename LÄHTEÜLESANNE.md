# shock_test – lähteülesanne

## 1. Eesmärk
Seade, mis mõõdab **poolsiinus-löögiimpulssi (Half-Sine Shock Pulse)** ja otsustab, kas
mõõdetud impulss vastab seadistatud nõuetele (**LÄBITUD / EBAÕNNESTUS**).

Alusstandard: **MIL-STD-810H, Method 516.8 – Shock** (`docs/MIL-STD-810H-Method-516.8-Shock.pdf`),
eelkõige joonis 516.8-5 „Half-Sine shock pulse configuration and tolerance limits“.

## 2. Riistvara
| Komponent | Kirjeldus |
|---|---|
| Plaat | Waveshare **ESP32-S3-Touch-LCD-4.3** (ESP32-S3, 800×480 RGB LCD, GT911 puutepaneel) |
| Andur | **ADXL375** – 3-teljeline kiirendusandur, ±200 g, 13-bit, 49 mg/LSB, ODR kuni 3200 Hz |
| Ühendus | **I2C**, ADXL375 aadress **0x53** (ALT ADDRESS = GND) |

Märkus: plaadi I2C siinil on juba GT911 (puute) ja CH422G (IO-laiendi) seadmed. Aadress 0x53 nendega ei kattu.

## 3. Standardi nõuded (MIL-STD-810H 516.8, joonis 516.8-5)
Tähistused: **A** – nominaalne tippkiirendus, **TD** – nominaalne impulsi kestus.

| Nõue | Väärtus | Allikas |
|---|---|---|
| Tippkiirenduse piirid | **0,8·A … 1,2·A** (±20 %) | joonis 516.8-5 |
| Kuju tolerants | kogu impulss peab jääma ideaalse poolsiinuse ±0,2·A koridori sisse | joonis 516.8-5 |
| Signaal enne/pärast impulssi | **±0,2·A** piires | joonis 516.8-5 |
| Kestuse tolerants | **TD ±10 %** | joonise võti |
| Kiiruse muutus (impulsi integraal) | nominaalist **±20 %**; nominaal ΔV = 2·A·TD/π | joonise võti |
| Jälgimisaken (löögimasin) | vähemalt **T1 = 2,4·TD** | joonise võti |
| Löökide arv | tavaliselt **3 lööki igas suunas igal teljel** (3 telge × 2 suunda = 18 lööki); haruldase sündmuse korral 1 löök suuna kohta | p 2.3.3 |
| Telg | iga test tehakse ühel teljel ja ühes suunas (±X, ±Y, ±Z). Katsekeha pööratakse, et katta kõik suunad | p 2.3.3, HSC |

Standardi näidisväärtused (eelseadistusteks):
| Test | A | TD |
|---|---|---|
| Procedure I, HSC-I (kiirlaevad) | 20 g | 23 ms |
| Procedure I, HSC-II | 5 g | 23 ms |
| Procedure I, maapealne, poolsiinus asendus (π/4 × 40 g) | ≈31 g | 11 ms |

### 3.1 UN38.3 – liitiumakude test T.4 „Shock“
Allikas: UN Manual of Tests and Criteria, osa III, p 38.3.4.4. Testitakse samu akusid/elemente, mis on läbinud testid T.1–T.3.

| Katseobjekt | Tippkiirendus A | TD |
|---|---|---|
| Element (cell) | 150 g | 6 ms |
| Suur element (alternatiiv) | 50 g | 11 ms |
| Väike aku | väiksem väärtustest 150 g ja √(100850 / m[kg]) g | 6 ms |
| Suur aku | väiksem väärtustest 50 g ja √(30000 / m[kg]) g | 11 ms |

- **Kuju:** poolsiinus.
- **Löökide arv:** 3 lööki kummaski suunas kolmes risti asendis, kokku **18 lööki**.
- **Akule esitatavad läbimise nõuded** (seade neid ise ei mõõda, operaator sisestab need raportisse):
  ei leki, ei vabasta gaasi, ei teki lühist, ei purune, ei plahvata ega sütti.
  Lisaks kontrollitakse samades testides kasutatavaid T.1–T.3 kriteeriume: massikadu ja avatud ahela pinge pärast testi ≥ 90 % testieelsest.
- **Eelseadistus:** seadetes valitakse katseobjekti tüüp ja sisestatakse mass. Programm arvutab A ja TD ise ning seab löökide jada 6 suunda × 3.

## 4. Funktsionaalsus

### 4.1 Seadistatavad parameetrid
| Parameeter | Vahemik / vaikimisi |
|---|---|
| Nominaalne tippkiirendus A | **5 … A_max** |
| A ülempiir A_max | **200 g** või **166 g** (seadetest valitav, vt p 6 küllastus). 166 g puhul mahub ülemine tolerants 1,2·A anduri mõõtepiiri |
| Impulsi kestus TD | ms, vt p 6 (soovitus 6 … 100 ms) |
| Mõõtetelg ja suund | ±X / ±Y / ±Z |
| Tolerantsid | vaikimisi standardi järgi: tipp ±20 %, TD ±10 %, ΔV ±20 % (muudetavad) |
| Löökide arv suuna kohta | vaikimisi 3 |
| Kuju koridori kontroll | sees / väljas (kas mõjutab LÄBITUD otsust või on ainult informatiivne) |
| Eelseadistus | MIL-STD-810H (p 3) või UN38.3 T.4 (p 3.1, koos massiga) |
| UN38.3 operaatori kontrollid | sees / väljas. Kui sees, küsib seade seeria alguses ja lõpus massi ja OCV ning seeria lõpus visuaalse kontrolli tulemuse; need lähevad raportisse |

Seaded salvestatakse püsimällu (NVS).

### 4.2 Vaated (puuteekraanil)
**A. Testi vaade**
- mõõdetud tippkiirendus g-des, suurelt
- **min (0,8·A)** ja **max (1,2·A)** piir
- mõõdetud kestus (ms) ja ΔV koos piiridega
- impulsi graafik koos tolerantsikoridoriga (ideaalne poolsiinus ±0,2·A)
- tulemus: **LÄBITUD** (roheline) / **EBAÕNNESTUS** (punane) ning põhjus (nt „tipp liiga madal“)
- järjeloendur: telg/suund ja löögi number (nt „+Z 2/3“)
- nupud: „Valmis / Arm“ ja „Lähtesta“
- kui kell pole sünkroniseeritud, kuvatakse teade: **„Kell seadistamata – ühenda WiFi-ga ja ava veebiportaal, et kella sünkroniseerida.“**

**B. Seadistuse vaade**
- A (5 g … A_max), A_max (200/166 g), TD (ms), telg/suund, tolerantsid, löökide arv
- kuju koridori kontroll sees/väljas, UN38.3 operaatori kontrollid sees/väljas
- standardi eelseadistused (p 3, p 3.1)
- WiFi seaded (p 4.4)
- salvestamine ja tagasi testi vaatesse

### 4.3 Testi loogika
1. Kasutaja vajutab „Valmis“ ja seade hakkab andurit pidevalt lugema (ADXL375 FIFO stream, 3200 Hz) ringpuhvrisse.
2. Kui valitud telje kiirendus ületab käivitusläve (nt 0,2·A), salvestatakse aken **≥ 2,4·TD** koos eelpuhvriga.
3. Arvutatakse:
   - **tipp** – valitud telje maksimum (märki arvestades)
   - **kestus** – aeg, mil kiirendus on üle 10 % tipust (kestuse definitsioon kinnitada)
   - **ΔV** – kiirenduse integraal üle impulsi
   - **kuju** – kas kõik punktid jäävad koridori sisse (ideaalne poolsiinus ±0,2·A, joondatud tipu järgi)
4. **LÄBITUD**, kui kehtivad kõik tingimused:
   `0,8·A ≤ tipp ≤ 1,2·A` **ja** `0,9·TD ≤ kestus ≤ 1,1·TD` **ja** `0,8·ΔV ≤ ΔV_mõõdetud ≤ 1,2·ΔV` **ja** (kui koridori kontroll on seadetes sisse lülitatud) kuju on koridori sees.

### 4.4 Tulemuste salvestamine ja WiFi
- Iga löögi tulemus ja toorandmed (valitud telje kõver) salvestatakse **seadme enda flash-mällu** (LittleFS).
  Mälu täitumisel antakse hoiatus.
- Seade avab **WiFi pääsupunkti koos captive portaaliga**. Telefoni või arvutiga ühendudes avaneb veebileht automaatselt.
- Veebilehel saab:
  - vaadata teste ja löökide seeriaid,
  - **alla laadida testraporti**,
  - kustutada vanu teste.
- **Kella sünkroniseerimine:** plaadil pole varupatareiga RTC-d. Portaali avamisel saadab brauser telefoni kellaaja seadmele
  ja seade seab oma kella. Kuni kell on seadistamata, kuvatakse ekraanil teade (p 4.2). Selle aja testid märgitakse
  raportis „kellaaeg seadistamata“ ja neile antakse käivitusest arvestatud järjekorranumber.
- WiFi (vaikimisi): pääsupunkt on alati sees, SSID `ShockTest-XXXX` (XXXX = MAC-i lõpp), WPA2 parool on seadetes muudetav.
- **Raporti formaadid:**
  - **PDF** – loetav testraport koos graafikutega. Genereeritakse seadmes, sest captive portaalis pole internetti ja välistest teekidest ei saa kasutada
  - **CSV** – iga löögi tulemused ja toorandmed (aeg, kiirendus) masinloetavalt
- **Püsivara uuendus üle WiFi captive portaali:** portaalis on leht „Püsivara“, kus kuvatakse praegune versioon ja saab
  valida uue `.bin` faili ning selle üles laadida. Uuendamise ajal näidatakse edenemist nii brauseris kui seadme ekraanil.
  Uuendamise ajaks lõpetatakse lööki ootamine; pooleli seeria jääb alles. Kui uus versioon ei käivitu
  või ei tööta 20 s stabiilselt, taastab alglaadur eelmise versiooni (rollback) ja ekraanil kuvatakse teade.
  Seaded ja salvestatud testid säilivad.
- **Püsivara allalaadimine:** uued versioonid avaldatakse GitHubi repos **https://github.com/eplik82/shock_test**
  (leht *Releases*, fail `firmware.bin`). Fail laaditakse esmalt telefoni või arvutisse ja seejärel portaali kaudu seadmesse.
  Portaali püsivara lehel on see viide kirjas.
- Katseobjekti andmed (nimi, seerianumber, operaator) sisestatakse seeria alguses puuteekraanil.
- Testraportis on: standard ja protseduur, seaded (A, TD, tolerantsid), iga löögi tulemus (telg/suund, tipp, kestus, ΔV, kuju, LÄBITUD/EBAÕNNESTUS),
  impulsi graafik koos tolerantsikoridoriga, koondtulemus, kuupäev ja kellaaeg, katseobjekti andmed ning UN38.3 puhul operaatori kontrollid.

## 5. Tarkvara
- PlatformIO + Arduino/ESP-IDF, LVGL graafika
- ADXL375 draiver (I2C 400 kHz, FIFO stream režiim)

## 6. Piirangud / riskid
- **Seade ei ole standardi mõttes vastav mõõtesüsteem.** 516.8 Annex A p 1.1 nõuab tipu kvalifitseerimiseks
  diskreetimissagedust ≥10 × f_max (vaikimisi f_max = 10 kHz, s.t 100 kS/s) ja analoogset anti-alias filtrit.
  ADXL375 ODR on maksimaalselt 3200 Hz (ribalaius ~1600 Hz). Seade sobib **kontroll- ja seadistusvahendiks**
  (stendi häälestus, kiirkontroll), mitte ametlikuks kvalifitseerimismõõtmiseks.
- Ajasamm on 0,3125 ms. Et TD ±10 % kontroll oleks usaldusväärne (resolutsioon ≤5 % TD-st), peab **TD ≥ ~6 ms**.
  Lühemat TD-d lubada ainult hoiatusega. Tüüpilised 11 ms ja 23 ms annavad vastavalt ~35 ja ~74 punkti impulsi kohta.
- **Küllastus:** ADXL375 mõõtepiir on ±200 g. Kui A > ~166 g, jääb ülemine piir 1,2·A anduri piirist välja
  (nt A = 200 g annab max 240 g). Üle 200 g tippu ei saa mõõta, seega tuleb mõõtepiiri saavutamisel kuvada „ANDUR KÜLLASTUNUD“.
- UN38.3 6 ms impulss annab ~19 mõõtepunkti. Sellest piisab, kuid TD ±10 % kontroll on piiripealne (samm on ~5 % TD-st).
- Andur peab olema testobjekti või stendi laua külge jäigalt kinnitatud.

## 7. Otsused
1. Tulemused salvestatakse seadme mällu ja raport laaditakse alla WiFi captive portaali kaudu (p 4.4).
2. A alampiir on 5 g (HSC-II).
3. Kuju koridori kontroll on seadetest sisse/välja lülitatav.
4. Raporti formaadid on PDF ja CSV.
5. Kui kell pole seadistatud, kuvatakse teade, et kasutaja ühenduks WiFi-ga ja sünkroniseeriks kella portaali kaudu.
6. UN38.3 operaatori kontrollid on seadetest sisse/välja lülitatavad.
7. A ülempiir on seadetest valitav: 200 g või 166 g.
8. Püsivara uuendatakse WiFi captive portaali kaudu (p 4.4), automaatse tagasipööramisega. Püsivara failid on GitHubi repos https://github.com/eplik82/shock_test (Releases).
9. Vaikevalikud (kasutaja pole muutnud): WiFi pääsupunkt on alati sees ja parooliga, katseobjekti andmed sisestatakse puuteekraanil.

## 8. Avatud küsimused
Lahtisi küsimusi pole. Vaikevalikuid p 7.9 saab vajadusel muuta.
