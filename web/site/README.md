# knobi-flash

<https://jhortale.github.io/knobi-flash/>: install [KNOBI-SIX](#source), an OB-6-inspired six-voice polysynth
firmware, on an M-VAVE SMK-37 Pro, SMK-37 Elite or MKE-P37 from Chrome or Edge, over USB (Web MIDI).

**Unofficial, experimental firmware, at your own risk.** Not made or endorsed by M-VAVE or Sequential. Before any
install, make a full flash backup of your keyboard: it is the only copy of its calibration and Bluetooth pairings, and
a browser cannot make it. The page asks you to confirm you have one.

This repository holds only the published site: the page, `catalog.json` (what it offers, with each package's
SHA-256), the release packages and `SHA256SUMS`. It is replaced on every release.

## Source

KNOBI-SIX and this installer are free software under the GNU General Public License, version 3 only (`LICENSE`).
Each release's complete source is published next to its packages:
`firmware/knobi-six/<version>/knobi-six-<version>-src.tar.gz` (linked from the page as "source"). The installer's
own source is in it, under `web/`.

## Credits

KNOBI-SIX is built on fc79's [smk37-base](https://github.com/fc79/smk37-base) (SMK BASE), from zvenson's
[Dinghy](https://github.com/zvenson/dinghy), on the platform of Leo Kuroshita's (Hügelton Instruments)
[Felucca](https://github.com/hugelton/Felucca) with the changes of [SLOOP](https://github.com/isod89/sloop-fm1) (isod89),
the SMK-37 port of M. Geisler (genkigenki/makkuro, [fm1-to-smk37](https://github.com/genkigenki/fm1-to-smk37)), and
OberVave's storage and loader screen (zvenson). The installer's update protocol is a port of their `fm1_install.py`.
The full list, with what each one made, is `LICENSING.md` in the source archive.

"M-VAVE", "SMK-37", "MKE-P37" and "OB-6" belong to their owners; "Felucca" and "Hügelton Instruments" are names of
Hügelton Instruments.
