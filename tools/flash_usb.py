"""USB-välgutus Windowsist (COM-port), ilma BOOT/RESET nuppudeta.
Töötav püsivara (shock_test või esp32-browser) viiakse seeriakäsuga 'dl' ROM-i allalaadimisrežiimi.
Kui plaat ei vasta, hoia BOOT all, vajuta RESET, vabasta BOOT - skript ootab 60 s.
Kasutus: python tools\\flash_usb.py [COM4]
"""
import os, struct, subprocess, sys, time
import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else 'COM4'
WAIT = float(sys.argv[2]) if len(sys.argv) > 2 else 60
HERE = os.path.dirname(os.path.abspath(__file__))
BIN = os.path.join(HERE, '..', 'bin')
ESPTOOL = [sys.executable, '-m', 'esptool', '--chip', 'esp32s3', '--port', PORT]

def open_port(baud):
    s = serial.Serial(); s.port = PORT; s.baudrate = baud; s.timeout = 0.3; s.dtr = False; s.rts = False; s.open()
    return s

def in_download_mode():
    r = subprocess.run(ESPTOOL + ['--before', 'no-reset', '--after', 'no-reset', 'chip-id'], capture_output=True)
    return r.returncode == 0

print('Saadan käsku "dl" korduvalt ...')
t = time.time()
while True:
    try:
        s = open_port(921600); s.write(b'\r\ndl\r\n'); time.sleep(0.25); s.close()
    except Exception as e:
        time.sleep(0.25)
    if in_download_mode():
        break
    if time.time() - t > WAIT:
        print('Allalaadimisrežiimi ei leitud'); sys.exit(2)
    print('ootan allalaadimisrežiimi (vajadusel BOOT+RESET) ...')
print('Allalaadimisrežiim leitud, välgutan ...')
r = subprocess.run(ESPTOOL + ['-b', '460800', '--before', 'no-reset', '--after', 'no-reset', 'write-flash', '-u',
                              '--flash-mode', 'keep', '--flash-freq', 'keep', '--flash-size', 'keep',
                              '0x0', os.path.join(BIN, 'bootloader.bin'), '0x8000', os.path.join(BIN, 'partitions.bin'),
                              '0xd000', os.path.join(BIN, 'ota_data_initial.bin'), '0x10000', os.path.join(BIN, 'firmware.bin')])
if r.returncode:
    print('VÄLGUTAMINE EBAÕNNESTUS', r.returncode); sys.exit(r.returncode)

# stub töötab veel: tühjenda sunnitud allalaadimise bitt ja tee süsteemilähtestus
def slip(b): return b'\xc0' + b.replace(b'\xdb', b'\xdb\xdd').replace(b'\xc0', b'\xdb\xdc') + b'\xc0'
s = open_port(460800)
def wr(addr, val):
    d = struct.pack('<IIII', addr, val, 0xFFFFFFFF, 0)
    s.write(slip(struct.pack('<BBHI', 0, 0x09, len(d), 0) + d)); time.sleep(0.05); return s.read(200)
ok = wr(0x6000812C, 0)
wr(0x60008000, 1 << 31)
s.close()
print('VALMIS - uus püsivara käivitub' if ok else 'VALMIS, aga stub ei vastanud - tõmba kaabel välja ja ühenda tagasi')
