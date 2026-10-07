// ADXL375 (±200 g) I2C 0x53: FIFO stream, pidev lugemine ringpuhvrisse
#pragma once
#include <stddef.h>
#include <stdint.h>

#define ADXL_LSB_G 0.049f  // 49 mg/LSB
#define ADXL_RANGE_G 200.0f

struct AccSample {
    int16_t x, y, z;  // toorväärtused (LSB)
};

bool adxl_init(int odr_hz);       // false kui andur ei vasta
bool adxl_present(void);
int adxl_odr(void);               // tegelik seatud ODR
// globaalne valimiloendur (kasvab igal valimil)
uint32_t adxl_count(void);
// kopeerib valimid [from, from+n) ringpuhvrist; tagastab kopeeritud arvu (vanad võivad olla üle kirjutatud)
size_t adxl_copy(uint32_t from, AccSample *dst, size_t n);
size_t adxl_ring_size(void);
// statistika
uint32_t adxl_overruns(void);     // FIFO täis (kadunud valimid)
float adxl_measured_rate(void);   // mõõdetud valimisagedus (Hz)
uint32_t adxl_i2c_errors(void);
// simulatsioon (testimiseks ilma löögimasinata): lisab sünteetilise poolsiinuse valitud teljele
void adxl_simulate(float peak_g, float td_ms, int axis /*0..2*/, float noise_g);
// viimane valim g-des (kuvamiseks)
void adxl_last_g(float *x, float *y, float *z);
// ID lugemise tulemus diagnostikaks
const char *adxl_id_text(void);
