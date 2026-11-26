#!/usr/bin/env python3
"""Stream one solid colour to every LED in direct mode for N seconds.
Direct mode is not stored: the keyboard returns to its own effect ~1s after
the stream stops. Usage: direct.py RRGGBB [seconds]"""
import fcntl, glob, os, sys, time
def ioc(d, t, nr, size): return (d << 30) | (size << 16) | (ord(t) << 8) | nr
def find():
    for h in sorted(glob.glob('/sys/class/hidraw/hidraw*')):
        if '0000258A:0000010C' not in open(h + '/device/uevent').read(): continue
        rd = open(h + '/device/report_descriptor', 'rb').read()
        if bytes.fromhex('0600ff') in rd and bytes.fromhex('8506') in rd:
            return '/dev/' + os.path.basename(h)
    sys.exit('not found')
rgb = bytes.fromhex(sys.argv[1]); secs = float(sys.argv[2]) if len(sys.argv) > 2 else 3
fd = os.open(find(), os.O_RDWR)
buf = bytearray(520); buf[0] = 6; buf[1] = 8; buf[4] = 1; buf[6] = 0x7A; buf[7] = 1
for i in range(126): buf[8 + i*3: 11 + i*3] = rgb
end = time.time() + secs; n = 0
while time.time() < end:
    fcntl.ioctl(fd, ioc(3, 'H', 0x06, 520), bytes(buf)); n += 1; time.sleep(0.1)
print('sent', n, 'frames ok')
