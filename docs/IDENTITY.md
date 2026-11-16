# KNOBI-SIX identity

What makes KNOBI-SIX distinct from the firmwares it descends from (SMK BASE, Dinghy, Felucca) so it never takes
another firmware's data or looks like it to the installers and the Firmware Hub.

| | Value | Where |
| --- | --- | --- |
| Name on screen | `KNOBI-SIX` | `APP_NAME`, `firmware/src/app.c`; `SCREEN_NAME`, `tools/build.py` (update loader) |
| Store magic | `0x36424E4B` ("KNB6", little-endian bytes `K N B 6`) | `STORE_MAGIC`, `firmware/src/app.c` |
| Data family (owner tag) | `KNOBI6` | `DATA_FAMILY`, `tools/build.py` (store.h: 1..7 of A-Z 0-9). SMK BASE used `SMKBASE`; a flash with another family's tag is cleared at first start. |
| Identity | `SMK-37 Pro_956` (`SMK-37 Elite_956`, `MKE-P37_956`) | `IDENT`, `tools/build.py`; `FELUCCA_ID` fallback in `firmware/src/platform.c` |
| Package | `build/knobi-six.fwsc` (`-elite`, `-mke`; releases `knobi-six-X.Y.fwsc`) | `tools/build.py` |

## Why IDENT 956

The installers' resume check wants `_9NN`, and the Firmware Hub shows a firmware only if its identity is not another's.
SMK BASE's `925` is not free any more: a firmware in the hub's catalog now ships `FM-1_925`.

Checked on 2026-11-16 against the Firmware Hub's catalog, `https://thefirmwarehub.com/catalog.json` (CC-BY-4.0, updated
2026-11-16 21:02 UTC, 228 firmwares):

1. Every firmware's `ident` is a regular expression over the product string. For each of `FM-1_900`..`FM-1_999` I tested
   `re.fullmatch(ident, product)` over all entries except the catch-all `FM-1_\d{3,8}` ones (stock-style, they claim
   nothing in particular). No pattern matched 901, 902, 904, 905, 907, 908, 909, 920, 925, 954, 956, 959, 962, 965,
   967, 984.
2. Every firmware's actual `product` field (what the shipped package carries) was also collected. Of the candidates
   from 1, `901`, `907`, `909`, `920` and `925` are in use there (the patterns undercount); the others are not.
3. Left free by both: 902, 904, 905, 908, 954, 956, 959, 962, 965, 967, 984. KNOBI-SIX takes **956**.

The catalog's `smk37` section lists only versions, stars and cards (no idents); SMK-37 firmwares appear in `firmwares`
under `FM-1_...` patterns, so the same table covers them. Firmwares not listed in the hub (private or unpublished) can
still hold 956; if a clash turns up, change `IDENT` in `tools/build.py` and `_956` in `platform.c` and re-run
`sh tests/run_tests.sh`.
