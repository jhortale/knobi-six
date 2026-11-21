# midi·chlorian logo

A cell that is also a 5-pin MIDI (DIN) connector: the membrane carries the connector's keyway, the five pins are
organelles linked to a glowing nucleus by faint signal threads (the cyan, violet, green and blue of the field, one warm
spark). The wordmark is lowercase, its dot the same nucleus.

| File | Use |
|---|---|
| `logo.svg` | the full logo on dark backgrounds (323 × 64) |
| `logo-light.svg` | the full logo on light backgrounds |
| `mark.svg` | the mark alone (64 × 64) |
| `favicon.svg` | the mark simplified for 16–32 px, on the night sky (also `apps/installer/public/favicon.svg`) |

In the page: `<Logo size={40} />` or `<Logo markOnly />` from `@knobi/ui/components/logo`, animated (the organelles
sway in 7 s, the nuclei breathe in 4 s; still with `prefers-reduced-motion`).

- Colours: membrane gradient `#5fe3ff` → `#9d8cff` → `#62f0b0`; organelles `#5fe3ff` `#9d8cff` `#ffb38a` `#62f0b0`
  `#3d7bff`; nucleus white → `#c9f6ff` → `#5fe3ff`; text `#f4f4f4` (dark) or `#14151a` (light).
- Type: Quicksand 600 (SIL OFL 1.1, `assets/fonts/Quicksand-LICENSE.txt`), tracking −6/1000, as outlines.
- Clear space: half the mark's height on every side. Smallest: 24 px high with the wordmark, 16 px for the mark.
- Original work: no franchise symbols, names or lettering.
