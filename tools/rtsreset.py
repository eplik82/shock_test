"""EN-lähtestus RTS kaudu ja käivituslogi lugemine: python tools\\rtsreset.py COM4 <sek>"""
import sys, time, serial
s = serial.Serial(); s.port = sys.argv[1]; s.baudrate = 921600; s.timeout = 0.1; s.dtr = False; s.rts = False; s.open()
s.rts = True; time.sleep(0.15); s.rts = False
out = b''; t = time.time()
while time.time() - t < float(sys.argv[2]): out += s.read(65536)
sys.stdout.buffer.write(out)
