#!/usr/bin/env python3
"""CH559 flasher (WCH bootloader v2.x), fixed for images larger than 11 KB.

Derived from chflasher.py by Aaron Christophel (https://ATCnetz.de). Two bugs
in that tool corrupt any re-flash of an image larger than 11 KB:

1. It erases a fixed 11 blocks of 1 KB for the CH559 (`device_erase_size =
   11`). Flash programming can only clear bits, so every byte past 11264 is
   written as (old AND new): the tail of the image is silently corrupted
   whenever that area already held data. A factory-blank chip is fine, which
   is why first flashes always worked.
2. Its write/verify check accepts status bytes 0xfe and 0xf5 as success, so
   the corrupted tail still printed "Verify success".

This version erases enough blocks to cover the whole image (rounded up, plus
one spare block), treats any non-zero status as a failure, and prints the
failing addresses. `--verify-only` compares the chip to a file without
erasing or writing anything, to diagnose a module before touching it.

Usage:
    python chflasher_fixed.py CH559USB.bin
    python chflasher_fixed.py --verify-only CH559USB.bin
Needs pyusb and the libusb-win32 driver (Zadig) for USB ID 4348:55E0.
"""
import sys
import time

import usb.core
import usb.util

DETECT_CHIP_V2 = (0xa1, 0x12, 0x00, 0x52, 0x11, 0x4d, 0x43, 0x55, 0x20, 0x49,
                  0x53, 0x50, 0x20, 0x26, 0x20, 0x57, 0x43, 0x48, 0x2e, 0x43, 0x4e)
MODE_WRITE = 0xa5
MODE_VERIFY = 0xa6
BLOCK = 1024
APP_LIMIT = 0xF000  # bootloader lives at 0xF400; never erase into it
PKT = 0x38


def die(msg):
    print('Error: ' + msg)
    sys.exit(1)


def main():
    args = sys.argv[1:]
    verify_only = '--verify-only' in args
    files = [a for a in args if not a.startswith('--')]
    if len(files) != 1:
        die('usage: chflasher_fixed.py [--verify-only] image.bin')
    image = open(files[0], 'rb').read()
    if len(image) < 256:
        die('image too small, wrong file?')
    if len(image) > APP_LIMIT:
        die('image larger than the application area')

    dev = usb.core.find(idVendor=0x4348, idProduct=0x55e0)
    if dev is None:
        die('no CH55x device in bootloader mode (USB 4348:55E0)')
    dev.set_configuration()
    intf = dev.get_active_configuration()[(0, 0)]
    epout = usb.util.find_descriptor(intf, custom_match=lambda e:
        usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_OUT)
    epin = usb.util.find_descriptor(intf, custom_match=lambda e:
        usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_IN)

    def cmd(data):
        epout.write(data)
        return epin.read(64)

    ident = cmd(DETECT_CHIP_V2)
    if len(ident) != 6:
        die('chip identification failed (bootloader v1 not supported here)')
    chipid = ident[4]
    if chipid != 0x59:
        die('not a CH559 (chip id 0x%02x)' % chipid)
    cfg = cmd((0xa7, 0x02, 0x00, 0x1f, 0x00))
    if len(cfg) != 30:
        die('bootloader identification failed')
    print('Found CH559, bootloader %d.%d%d' % (cfg[19], cfg[20], cfg[21]))
    # Key exchange: the bootloader XORs every 8th byte with this key.
    key = bytearray(64)
    key[0], key[1] = 0xa3, 0x30
    checksum = (cfg[22] + cfg[23] + cfg[24] + cfg[25]) & 0xff
    for x in range(0x30):
        key[x + 3] = checksum
    cmd(key)

    if not verify_only:
        blocks = (len(image) + BLOCK - 1) // BLOCK + 1
        blocks = min(blocks, APP_LIMIT // BLOCK)
        r = cmd((0xa4, 0x01, 0x00, blocks))
        if r[4] != 0x00:
            die('erase failed (status 0x%02x)' % r[4])
        print('Erased %d blocks (%d bytes) for a %d-byte image' %
              (blocks, blocks * BLOCK, len(image)))

    def transfer(mode):
        bad = []
        addr, left = 0, len(image)
        while addr < len(image):
            n = min(PKT, left)
            out = bytearray(64)
            out[0] = mode
            out[1] = n + 5
            out[3] = addr & 0xff
            out[4] = (addr >> 8) & 0xff
            out[7] = left & 0xff
            out[8:8 + n] = image[addr:addr + n]
            for x in range(n + 8):
                if x % 8 == 7:
                    out[x] ^= chipid
            r = cmd(out)
            if r[4] != 0x00:
                bad.append((addr, n, r[4]))
            addr += n
            left -= n
        return bad

    if not verify_only:
        bad = transfer(MODE_WRITE)
        if bad:
            for a, n, s in bad[:10]:
                print('  write status 0x%02x at 0x%04x..0x%04x' % (s, a, a + n - 1))
            die('write failed on %d packet(s)' % len(bad))
        print('Write OK')

    bad = transfer(MODE_VERIFY)
    if bad:
        for a, n, s in bad[:20]:
            print('  verify status 0x%02x at 0x%04x..0x%04x' % (s, a, a + n - 1))
        print('VERIFY FAILED: %d packet(s) differ, first at 0x%04x' % (len(bad), bad[0][0]))
        if verify_only:
            sys.exit(3)
        die('verify failed')
    print('Verify OK (%d bytes)' % len(image))

    if verify_only:
        return
    epout.write((0xa2, 0x01, 0x00, 0x01))  # leave bootloader, run the app
    try:
        epin.read(64, timeout=2000)
    except Exception:
        pass  # the chip resets right away; no answer is the normal case
    time.sleep(0.5)
    print('Bootloader exit sent. Power-cycle the module if it does not answer.')


if __name__ == '__main__':
    main()
