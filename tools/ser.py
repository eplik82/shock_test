"""Seeriaport: python tools\\ser.py COM4 <sek> [käsk ...] - loeb logi, saadab käsud."""
import sys, time, serial
port, secs = sys.argv[1], float(sys.argv[2])
cmds = sys.argv[3:]
s = serial.Serial(); s.port = port; s.baudrate = 921600; s.timeout = 0.1; s.dtr = False; s.rts = False; s.open()
out = b''
t = time.time()
for c in cmds:
    s.write(c.encode() + b'\n'); time.sleep(0.3)
while time.time() - t < secs:
    out += s.read(65536)
sys.stdout.buffer.write(out)
