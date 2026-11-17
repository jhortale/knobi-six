#!/usr/bin/env python3
"""(Copied from jhortale/smk37-analog, spike/device-link, spike/cpu/, by João Hortale.) Flatten an ELF's PT_LOAD segments by physical address into an image based at 0x02000000."""
import struct, sys
e = open(sys.argv[1], 'rb').read()
phoff = struct.unpack_from('<I', e, 28)[0]
phentsize, phnum = struct.unpack_from('<HH', e, 42)
base, img = 0x02000000, bytearray()
for i in range(phnum):
    t, off, vaddr, paddr, filesz, memsz, flags, align = struct.unpack_from('<8I', e, phoff + i * phentsize)
    if t != 1 or not filesz:
        continue
    if paddr < base:  # the ELF/program headers mapped in front of .boot: drop them
        skip = base - paddr
        if skip >= filesz:
            continue
        off, filesz, paddr = off + skip, filesz - skip, base
    at = paddr - base
    if len(img) < at + filesz:
        img.extend(b'\0' * (at + filesz - len(img)))
    img[at:at + filesz] = e[off:off + filesz]
open(sys.argv[2], 'wb').write(img)
print(f'{sys.argv[2]}: {len(img)} bytes')
