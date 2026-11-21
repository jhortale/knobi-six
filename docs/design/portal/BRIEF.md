# Design brief: midi-chlorian

## What it is

**midi-chlorian** is a free website where people install open-source firmwares ("synths") onto their MIDI keyboards
straight from the browser (Chrome or Edge, over USB). The first synth is **KNOBI-SIX**, a six-voice polysynth for the
M-VAVE SMK-37 keyboard family. More synths will follow. The name is a pun: MIDI + the tiny symbiotic life forms that,
in a famous space saga, connect living beings to an energy field.

**Audience:** musicians and hardware tinkerers who own one of these cheap keyboards and want to turn it into a real
synth. They're curious, a bit technical, and a little nervous: they are rewriting their keyboard's firmware.

**What a visit should feel like:** wonder, then calm confidence. The site is playful and alive, but the moment someone
installs, it becomes clear, precise and trustworthy.

## Theme and motion

Explore the idea of **microscopic, bioluminescent organisms drifting in deep space**: a living energy field that links
the player, the keyboard and the music. Think plankton glowing in a night ocean, spores, cells, nebula dust, faint
signal lines between particles, slow orbits, symbiosis.

**Make it original.** No Star Wars names, symbols, characters, lightsabers, logos or lookalike fonts, and don't use the
words "Jedi" or "the Force". Evoke the feeling, not the franchise.

Animation ideas to explore (pick, combine, improve):

- **Ambient field.** A slow, sparse field of glowing particles behind the content, drifting and occasionally linking
  with thin light threads. It reacts softly to the cursor: particles lean toward it.
- **Synth cards.** On hover, the particles gather around the card. Each synth could have its own colour signature.
- **Connect keyboard.** While searching, particles pulse outward like a sonar. When the keyboard is found, they
  converge into a small "colony" around the device status: something alive just found its host.
- **Install / write.** Particles stream into the keyboard, one stream per data block, in step with the real progress
  bar (0–100 %). The six steps (Connect, Verify, Update mode, Write, Restart, Done) light up like LEDs.
- **Done.** A gentle bloom, then the field settles brighter than before.
- **Errors.** The particles dim and scatter, never alarming. The message and its next step are what stands out.
- **Idle on a synth page.** A faint rhythmic shimmer, as if the organisms breathe in time with music.

**Motion rules:**

- Respect `prefers-reduced-motion`: a static, still-beautiful version with no movement.
- **Performance is a hard constraint.** During an install, the page answers the keyboard's data requests in real time.
  Animations must never block the main thread: use CSS transforms/opacity or a lightweight canvas/WebGL layer. Calm the
  field while writing (fewer particles, lower frame rate), and never allocate heavily during the write.
- Phones at 375 px wide: fewer particles, nothing that drains the battery.

## Look

Two layers that meet:

1. **The cosmos:** a deep black-blue background, luminous particles in cool cyans, violets and greens, with occasional
   warm sparks.
2. **The hardware:** the installer panel keeps the KNOBI-SIX instrument look, a dark panel with a black frame, a
   walnut-wood strip, blue key accents and red LEDs.

   Palette: panel `#1d1e22`, text `#f4f4f4`, muted `#9aa0aa`, key blue `#2f6be0`, LED red `#ff3524`, ok green `#4fd18b`,
   warm warning `#ffb38a`, wood `#5a2a14` → `#8a4a26`.

   Each synth can bring its own panel look; midi-chlorian itself is the cosmos around them.

**Type:** a rounded sans for the UI (today: "Arial Rounded MT Bold", Helvetica Neue). The wordmark "midi·chlorian" can
be custom, lowercase, with a glowing dot. Use only free fonts.

Always dark. Accessible: text contrast at least WCAG AA over every animated background, visible focus states, full
keyboard navigation.

## Pages and states

**Home**
- The wordmark, one tagline ("Firmwares for MIDI keyboards, installed from the browser"), and the line
  "Unofficial · not made by M-VAVE or Sequential".
- A grid of synth cards: name, one-line summary, current version, supported keyboards; a "coming" card for future
  synths.
- A short "how it works" (pick a synth, plug in with a USB data cable, press Install; Chrome or Edge).

**Synth page (KNOBI-SIX)**
- A breadcrumb, then the header: name, description, and a side box (current release and date, keyboards, desktop status
  "VST3 / AU coming", licence GPL-3.0 with a source link).
- **Install on my keyboard**, the heart of the page. Design every state:
  - **Idle:** "Connect keyboard" button, "search all MIDI ports" option, "Plug the keyboard in with a USB data cable."
  - **Searching** (a few seconds).
  - **Found, running:** "✓ SMK-37 Pro_016 on "X0X FM-1" (running)".
  - **Found in update mode:** the last install didn't finish, and Install finishes it (with a "finish the install"
    badge).
  - **Found, but in another firmware's update mode:** refused, with an explanation.
  - **No keyboard found:** lists the MIDI inputs seen.
  - **No Web MIDI** (wrong browser, or permission denied): use Chrome or Edge, over https, allow MIDI access.
  - **The install warning, always visible:** "Replaces the stored firmware and its saved data; calibration is not
    written."
  - **The backup checkbox:** "I have a verified full flash backup of this keyboard (how)", with "The backup is the only
    copy of your keyboard's calibration and pairings." Install stays disabled until it is ticked. This must stay
    unmissable; never hide it behind effects.
  - **The confirm dialog** before writing: which firmware, onto which keyboard, and "do not unplug or close this tab".
  - **Writing:** the six steps, a progress bar, and "keep the cable in".
  - **Done:** "the keyboard runs SMK-37 Pro_956".
  - **Errors, each with its next step:** unplugged during the write (plug back in, Connect, Install again: it finishes);
    stopped responding; the site was just updated (reload the page); refused, wrong model.
  - **The log:** collapsible, with "Copy log".
- **Download to my computer:** a table per keyboard model (the `.fwsc` file, size, SHA-256), the GPL source archive, an
  "all checksums" link, "Built by CI from the tag knobi-six-v1.0", and a reserved row for the desktop VST3 / AU
  downloads (Mac, Windows) that come later.
- **Releases:** the current release's notes, then older releases folded, each with its own install and downloads.
- **Footer:** free software (GPL-3.0-only); M-VAVE, SMK-37, MKE-P37 and OB-6 belong to their owners.

## Technical fit

The site is built with **React 19 + Tailwind CSS v4 + shadcn/ui** (Button, Checkbox, RadioGroup, Card, Progress, Alert,
AlertDialog, Collapsible, Badge, Label), themed through CSS variables, and served as a static site (GitHub Pages).
Designs that map onto these components and tokens are easiest to build. The animation layer should be one
self-contained component (CSS, or a small canvas/WebGL module), with a reduced-motion fallback.

## Deliverables

1. Home and the KNOBI-SIX page, desktop (1280 px) and phone (375 px).
2. Every installer state listed above, on desktop and phone.
3. The motion spec: what moves, when, durations and easing; how it behaves during the write; the reduced-motion
   version.
4. Design tokens: colours, type scale, spacing, radii, glows.
5. If possible, a clickable prototype (HTML/React) of the install flow with the animations.

## Attached

`mockup.html`: today's low-fidelity mockup. Its structure and copy are right, its look is a placeholder.
