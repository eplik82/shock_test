"""Ootab porti ja salvestab logi: python tools\\bootlog.py COM4 <sek> <fail>"""
import sys, time, serial
port, secs, fn = sys.argv[1], float(sys.argv[2]), sys.argv[3]
t = time.time(); out = b''; s = None
with open(fn, 'wb') as f:
    while time.time() - t < secs:
        if s is None:
            try:
                s = serial.Serial(); s.port = port; s.baudrate = 921600; s.timeout = 0.2; s.dtr = False; s.rts = False; s.open()
                f.write(b'\n[port avatud %.1f s]\n' % (time.time() - t)); f.flush()
            except Exception:
                s = None; time.sleep(0.2); continue
        try:
            d = s.read(65536)
            if d: f.write(d); f.flush()
        except Exception:
            f.write(b'\n[port kadus]\n'); f.flush()
            try: s.close()
            except Exception: pass
            s = None
