#!/usr/bin/env python3
"""KNOBI-SIX CPU gate (copied from jhortale/smk37-analog, spike/device-link, spike/cpu/patch_emu.py, by João Hortale):
patch a checkout of joakimeriksson/fm1-emu (pinned commit) so that `fm1-emu`
records, per pc, instructions and cycle costs under two models when FM1_SPIKE_COST=FILE:
  cost8: fm1-emu's own model (eighths of a cycle; harbor's 2026-10-24 FM-1 probe: ALU ~0.6,
         two plain ops a cycle);
  alt8:  the 2026-11-15 FM-1 probe (jvitkauskas/pi32v2-isa hw/fm1-2026-11-15-excprobe group 7,
         C0_TL_CKCNTL): ALU 1 (no dual issue), 6-byte constant 1.46, load 2, store 2, mul 2,
         div 10, FPU add/mul 2, div 4, taken branch/goto 7.5, untaken 1, call/goto/rts 7.5 each,
         pair load 4."""
import sys
root = sys.argv[1]
lib = f'{root}/crates/fm1-core/src/lib.rs'
s = open(lib).read()
a = """        bus.add_ticks(c.saturating_sub(1) as u64);
        Ok(())"""
b = """        bus.add_ticks(c.saturating_sub(1) as u64);
        let alt = match (fast, named) {
            (Some(f), _) => spike_alt_cost8(&f, len, taken, self.pc < pc),
            (None, Some((name, raw))) => if taken { 60 } else { named_cost8(name, raw, false).max(8) },
            (None, None) => if taken { 60 } else { 8 },
        };
        if let Some(m) = SPIKE_COST.lock().unwrap().as_mut() {
            let e = m.entry(pc).or_insert((0, 0, 0));
            e.0 += 1; e.1 += c as u64; e.2 += alt as u64;
        }
        Ok(())"""
assert s.count(a) == 1, 'step patch'
s = s.replace(a, b)
a = "fn op_cost8(f: &Fast, len: u8, taken: bool, back: bool) -> u32 {"
b = """/// SPIKE: per-pc (instructions, fm1-emu cost8, alternative cost8), when enabled.
pub static SPIKE_COST: std::sync::Mutex<Option<std::collections::HashMap<u32, (u64, u64, u64)>>> = std::sync::Mutex::new(None);

/// SPIKE: the 2026-11-15 hardware table, in eighths of a cycle (see spike/cpu/patch_emu.py).
fn spike_alt_cost8(f: &Fast, len: u8, taken: bool, _back: bool) -> u32 {
    let mask = |m: u16| m.count_ones();
    let range = |n: u8| (n as u32).saturating_sub(3);
    match *f {
        Fast::MovI { .. } if len >= 6 => 12,
        Fast::Alu3 { op: Op::Mul, .. } | Fast::Alu3I { op: Op::Mul, .. } | Fast::Alu2 { op: Op::Mul, .. }
        | Fast::Alu2I { op: Op::Mul, .. } => 16,
        Fast::Div { .. } => 80,
        Fast::Ld { .. } | Fast::LdW { .. } | Fast::LdWSp { .. } | Fast::LdBu { .. } | Fast::LdIdx { .. }
        | Fast::LdPost { .. } | Fast::LdPre { .. } | Fast::LdPostR { .. } => 16,
        Fast::St { .. } | Fast::StW { .. } | Fast::StWSp { .. } | Fast::StIdx { .. } | Fast::StPost { .. }
        | Fast::StPre { .. } | Fast::StPostR { .. } => 16,
        Fast::LdPair { .. } => 32,
        Fast::StPair { .. } => 32,
        Fast::Rmw { .. } => 32,
        Fast::PushMask { mask: m } | Fast::PopMask { mask: m } => 16 + 8 * mask(m),
        Fast::BlockMove { mask: m, .. } => 16 + 8 * mask(m),
        Fast::PushRets { n } => 24 + 8 * range(n),
        Fast::PopPc { n } => 60 + 8 * range(n),
        Fast::PushRet | Fast::PopRets => 24,
        Fast::PopRet => 60,
        Fast::Call { .. } | Fast::CallR { .. } | Fast::Rts => 60,
        Fast::Goto { .. } | Fast::GotoR { .. } | Fast::Tb { .. } => 60,
        Fast::BrI { .. } | Fast::BrR { .. } | Fast::BrIEq { .. } | Fast::BrINe { .. } | Fast::BitBr { .. }
        | Fast::DecBr { .. } => if taken { 60 } else { 8 },
        Fast::FOp { op: 0..=2, .. } => 16,
        Fast::FOp { op: 3, .. } => 32,
        Fast::FOp { .. } => 16,
        Fast::Raw { raw, .. } if raw & 0xff00 == 0xe100 && matches!(raw & 0xfe, 0xf8 | 0xfc | 0xfe) => 32,
        _ => if taken { 60 } else { 8 },
    }
}

fn op_cost8(f: &Fast, len: u8, taken: bool, back: bool) -> u32 {"""
assert s.count(a) == 1, 'cost fn'
s = s.replace(a, b)
open(lib, 'w').write(s)

main = f'{root}/crates/fm1-emu/src/main.rs'
s = open(main).read()
a = "    let mut cpu = Cpu::new(args.entry);\n"
b = a + """    if std::env::var_os("FM1_SPIKE_COST").is_some() {
        cpu.cost_model = true;
        *fm1_core::SPIKE_COST.lock().unwrap() = Some(Default::default());
    }
"""
assert s.count(a) == 1, 'main cpu'
s = s.replace(a, b)
a = "    let cpu = &cpus[report_core];\n"
b = """    if let Some(path) = std::env::var_os("FM1_SPIKE_COST") {
        if let Some(m) = fm1_core::SPIKE_COST.lock().unwrap().as_ref() {
            let mut rows: Vec<_> = m.iter().collect();
            rows.sort();
            let mut text = String::new();
            let (mut n, mut c, mut a) = (0u64, 0u64, 0u64);
            for (pc, (k, c8, a8)) in rows {
                text.push_str(&format!("{pc:#010x} {k} {c8} {a8}\\n"));
                n += k; c += c8; a += a8;
            }
            let _ = std::fs::write(&path, text);
            println!("SPIKE insns {n} cost8 {c} alt8 {a}");
        }
    }
""" + a
assert s.count(a) == 1, 'main end'
s = s.replace(a, b)
open(main, 'w').write(s)
print('patched')
