#!/usr/bin/env python3
"""Read-only probe: asks the keyboard for its info block and prints it.

Sends the same query OpenRGB sends on detection (06 82 01 00 01 00 06) and
reads feature report 6 back. Nothing is stored on the keyboard.
"""
import fcntl, glob, os, sys

def ioc(d, t, nr, size): return (d << 30) | (size << 16) | (ord(t) << 8) | nr
HIDIOCSFEATURE = lambda n: ioc(3, 'H', 0x06, n)
HIDIOCGFEATURE = lambda n: ioc(3, 'H', 0x07, n)

def find():
    for h in sorted(glob.glob('/sys/class/hidraw/hidraw*')):
        ue = open(h + '/device/uevent').read()
        if '0000258A:0000010C' not in ue: continue
        rd = open(h + '/device/report_descriptor', 'rb').read()
        if bytes.fromhex('0600ff') in rd and bytes.fromhex('8506') in rd:
            return '/dev/' + os.path.basename(h)
    sys.exit('keyboard lighting interface not found')

path = find()
try:
    fd = os.open(path, os.O_RDWR)
except PermissionError:
    sys.exit(f'no permission on {path} - install the udev rule first')
cmd = bytearray(520); cmd[:7] = bytes([0x06, 0x82, 0x01, 0x00, 0x01, 0x00, 0x06])
fcntl.ioctl(fd, HIDIOCSFEATURE(520), cmd)
buf = bytearray(520); buf[0] = 0x06
n = fcntl.ioctl(fd, HIDIOCGFEATURE(520), buf)
print(path, 'len', n)
print('model id (byte 13): 0x%02X' % buf[13])
for i in range(0, 64, 16): print('%03x  %s' % (i, buf[i:i+16].hex(' ')))
print('nonzero bytes beyond 64:', sum(1 for b in buf[64:] if b))
