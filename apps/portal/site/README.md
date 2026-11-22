# midi-chlorian

<https://jhortale.github.io/midi-chlorian/>: firmwares for MIDI keyboards, installed from the browser (Web MIDI, Chrome
or Edge, over USB). Today: [KNOBI-SIX](https://jhortale.github.io/midi-chlorian/knobi-six/), an OB-6-inspired
six-voice polysynth firmware for the M-VAVE SMK-37 Pro, SMK-37 Elite and MKE-P37.

**Unofficial, experimental firmware, at your own risk.** Not made or endorsed by M-VAVE or Sequential. Before any
install, make a full flash backup of your keyboard: it is the only copy of its calibration and Bluetooth pairings, and
a browser cannot make it. The installer asks you to confirm you have one.

This repository holds only the published site, replaced on every deploy: the pages, `catalog.json` (what the installer
offers, with each package's SHA-256), the release packages with their `SHA256SUMS`, and the source archive.

## Source

The site, the installer and the firmware are free software under the GNU General Public License, version 3 only
(`LICENSE`). The complete source of the deployed site, the whole repository at the deployed commit, is
`source/midi-chlorian-src-<commit>.tar.gz` (linked from every page's footer, `source/`). Each release's own source is
published next to its packages: `firmware/knobi-six/<version>/knobi-six-<version>-src.tar.gz`. The upstream repository
is <https://github.com/jhortale/midi-chlorian>.

## Credits

The full list, with what each one made, is `LICENSING.md` inside the source archive. In short: KNOBI-SIX is built on
fc79's [smk37-base](https://github.com/fc79/smk37-base) (SMK BASE), from zvenson's
[Dinghy](https://github.com/zvenson/dinghy), on the platform of Leo Kuroshita's (Hügelton Instruments)
[Felucca](https://github.com/hugelton/Felucca) with the changes of [SLOOP](https://github.com/isod89/sloop-fm1) (isod89),
the SMK-37 port of M. Geisler (genkigenki/makkuro, [fm1-to-smk37](https://github.com/genkigenki/fm1-to-smk37)), and
OberVave's storage and loader screen (zvenson). The installer's update protocol is a port of their `fm1_install.py`.

"M-VAVE", "SMK-37", "MKE-P37" and "OB-6" belong to their owners; "Felucca" and "Hügelton Instruments" are names of
Hügelton Instruments.
