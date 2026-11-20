// SPDX-License-Identifier: GPL-3.0-only
// @knobi/device-sim: a simulated keyboard (a port of tests/install_test.py's FakeFM1) and a package builder (its
// package()): Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments; M. Geisler (genkigenki/makkuro).
// For tests and the dev server's ?mock page only.
export { FakeKeyboard, type FakeOpts } from "./fakeDevice.ts";
export { makePackage, type PackageOpts } from "./makePackage.ts";
