"""Seeria kaudu kaugjuhtimine ja ekraanipildid.
python tools\\sershot.py COM4 out_dir "tap 100 453" "sleep 1" "shot nimi" "sim 50 11 2 0.3" ...
Ekraanipilt salvestatakse <out_dir>/<nimi>.rle (u16 kordus, u16 RGB565)."""
import base64, os, sys, time, serial
port, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
s = serial.Serial(); s.port = port; s.baudrate = 921600; s.timeout = 0.1; s.dtr = False; s.rts = False; s.open()
def read_for(secs, stop=None):
    d = b''; t = time.time()
    while time.time() - t < secs:
        d += s.read(65536)
        if stop and stop in d: break
    return d
cmds = sys.argv[3:]
if len(cmds) == 1 and cmds[0].endswith(".txt"):
    cmds = [l.strip() for l in open(cmds[0], encoding="utf-8") if l.strip() and not l.startswith("#")]
for c in cmds:
    if c.startswith('sleep '):
        time.sleep(float(c.split()[1])); continue
    if c.startswith('shot '):
        name = c.split()[1]
        s.reset_input_buffer(); s.write(b'\nshot\n')
        data = read_for(40, b'@SEND')
        lines = data.decode(errors='replace').splitlines()
        try:
            i = next(k for k, l in enumerate(lines) if l.startswith('@SBEGIN'))
        except StopIteration:
            print('pilti ei tulnud:', name); continue
        b64 = ''.join(l[2:].strip() for l in lines[i + 1:] if l.startswith('@S') and not l.startswith('@SEND'))
        open(os.path.join(out, name + '.rle'), 'wb').write(base64.b64decode(b64))
        print('pilt', name); continue
    s.write(c.encode() + b'\n'); time.sleep(0.3)
    print('käsk', c)
s.close()
