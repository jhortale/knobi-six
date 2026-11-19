#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""knobi_core's per-instance state (plugin plan Task 2), written from the firmware's own declarations.

The firmware modules the core compiles keep their mutable file-scope state as they always had it (the device build is
unchanged: its preprocessed tokens are the same). Each block of it is wrapped like this:

    #ifdef KNOBI_CORE
    #include "core/state/engine.h"     (knobi_core: these per instance; tools/core_state.py)
    #else
    static synth_t eng_synth;
    static volatile uint32_t eng_qw, eng_qr, eng_drops;
    static int32_t eng_last_bend = 8192, ...;
    #define ENG_FADE_ONE 32768
    #endif

and this script writes firmware/src/core/state/<name>.h from the lines after #else: the #define lines as they are, a
struct kc_st_<name> with one field per variable (type, array size), kc_st_<name>_init (the initial values, all else
zero, as a static's), the block's slot KC_IX_<name> and a macro per variable,
`#define eng_synth (KC_ST(engine)->eng_synth)`: KC_ST (knobi_core.c) is the block of the instance this thread runs
(kc_slots(): a pthread key's value, set by knobi_core.c on every call), so the firmware's code, unchanged, reads and
writes the instance's field. state/list.h lists the blocks for knobi_core.c.
Inside a block: static declarations (any number of declarators, an initializer each, array sizes, __attribute__s),
#define lines, comments, and #ifdef DINGHY_HOST / #else / #endif (the core is a host build: the first branch).

    python3 tools/core_state.py           write the headers
    python3 tools/core_state.py --check   exit 1 if a header is not what the firmware's lines give (make -C tests unit)
"""
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SRC = os.path.join(ROOT, "firmware", "src")
OUT = os.path.join(SRC, "core", "state")
# the files knobi_core.c compiles that hold blocks, in its order
FILES = ["params/prog_seq.c", "engine.c", "ui/controls.c", "ui/midi_map.c"]

OPEN = re.compile(r'^\s*#\s*ifdef\s+KNOBI_CORE\b')
INC = re.compile(r'^\s*#\s*include\s+"(?:\.\./)*core/state/(\w+)\.h"')


def fail(msg):
    sys.exit("core_state.py: " + msg)


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def split_top(text, sep):
    """split at sep outside (), [], {}"""
    out, depth, cur = [], 0, ""
    for ch in text:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == sep and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    out.append(cur)
    return out


def norm(s):
    return " ".join(s.split())


def parse_declarator(d, src):
    """'name[dims] __attribute__((...)) = init' -> (name, dims, init)"""
    parts = split_top(d, "=")
    lhs, init = parts[0], ("=".join(parts[1:]).strip() if len(parts) > 1 else None)
    lhs = re.sub(r"__attribute__\s*\(\(.*?\)\)", " ", lhs).strip()
    dims = ""
    while lhs.endswith("]"):
        depth, i = 0, len(lhs) - 1
        while i >= 0:
            if lhs[i] == "]":
                depth += 1
            elif lhs[i] == "[":
                depth -= 1
                if depth == 0:
                    break
            i -= 1
        dims = lhs[i:] + dims
        lhs = lhs[:i].rstrip()
    m = re.search(r"([A-Za-z_]\w*)$", lhs)
    if not m:
        fail(f"{src}: no name in '{d.strip()}'")
    return lhs[:m.start()].strip(), m.group(1), norm(dims), (norm(init) if init else None)


def parse_block(lines, src):
    """the lines after #else up to the block's #endif -> (defines, vars [(type, name, dims, init)])"""
    defines, code, skip, nest = [], [], False, []
    i = 0
    while i < len(lines):
        line = lines[i]
        s = line.strip()
        if s.startswith("#"):
            d = re.sub(r"^#\s*", "#", s)
            if d.startswith("#define"):
                block = [line.rstrip("\n")]
                while block[-1].endswith("\\"):
                    i += 1
                    block.append(lines[i].rstrip("\n"))
                if not skip:
                    defines.extend(block)
            elif re.match(r"#ifdef\s+DINGHY_HOST\b", d):
                nest.append("host")
            elif d.startswith("#else") and nest:
                skip = True
            elif d.startswith("#endif") and nest:
                nest.pop()
                skip = False
            else:
                fail(f"{src}: '{s}' in a state block (only #define and #ifdef DINGHY_HOST / #else / #endif)")
        elif not skip:
            code.append(line)
        i += 1
    variables = []
    for stmt in split_top(strip_comments("".join(code)), ";"):
        stmt = norm(stmt)
        if not stmt:
            continue
        if not stmt.startswith("static "):
            fail(f"{src}: not a static declaration: '{stmt}'")
        decls = split_top(stmt[len("static "):], ",")
        typ, name, dims, init = parse_declarator(decls[0], src)
        if not typ:
            fail(f"{src}: no type in '{stmt}'")
        if len(decls) > 1 and typ.rstrip().endswith("*"):   # (static T *a, b: b is a T, not a T *)
            fail(f"{src}: '{stmt}': a pointer declarator with others: one declaration each")
        variables.append((typ, name, dims, init))
        for d in decls[1:]:
            t2, name, dims, init = parse_declarator(d, src)
            if t2:
                fail(f"{src}: '{d.strip()}': one type a declaration")
            variables.append((typ, name, dims, init))
    return defines, variables


def find_blocks():
    blocks = []
    for rel in FILES:
        path = os.path.join(SRC, rel)
        lines = open(path).readlines()
        i = 0
        while i < len(lines):
            if OPEN.match(lines[i]) and i + 2 < len(lines) and INC.match(lines[i + 1]) and \
                    re.match(r"^\s*#\s*else\b", lines[i + 2]):
                name = INC.match(lines[i + 1]).group(1)
                j, depth = i + 3, 1
                body = []
                while j < len(lines):
                    s = lines[j].strip()
                    if re.match(r"#\s*if", s):
                        depth += 1
                    elif re.match(r"#\s*endif", s):
                        depth -= 1
                        if depth == 0:
                            break
                    body.append(lines[j])
                    j += 1
                if depth:
                    fail(f"{rel}: block {name}: no #endif")
                blocks.append((rel, name, i + 4, body))
                i = j
            i += 1
    names = [b[1] for b in blocks]
    if len(set(names)) != len(names):
        fail("a block name twice: " + " ".join(names))
    return blocks


def header(rel, name, ix, defines, variables):
    st = f"kc_st_{name}"
    o = ["/* SPDX-License-Identifier: GPL-3.0-only */",
         f"/* Written by tools/core_state.py from firmware/src/{rel} (a state block): do not edit. The",
         " * firmware's state there, per knobi_core instance: the struct, the initial values, its slot and a macro per",
         " * variable (KC_ST: the block of the instance this thread runs, knobi_core.c kc_bind). Not for the device build. */",
         f"#ifndef KC_STATE_{name.upper()}_H", f"#define KC_STATE_{name.upper()}_H"]
    o += defines
    o.append(f"struct {st} {{")
    for t, n, d, _ in variables:
        o.append(f"    {t} {n}{d};")
    o.append("};")
    o.append(f"#define KC_IX_{name} {ix}")
    o.append(f"static void {st}_init(struct {st} *s)")
    o.append("{")
    o.append("    memset((void *)s, 0, sizeof *s);")
    for t, n, d, init in variables:
        if init is None:
            continue
        if init.startswith("{"):
            ct = norm(re.sub(r"\bvolatile\b", "", t))
            o.append(f"    {{ static const {ct} v{d} = {init}; memcpy((void *)s->{n}, v, sizeof v); }}")
        else:
            o.append(f"    s->{n} = {init};")
    o.append("}")
    for t, n, d, _ in variables:
        o.append(f"#define {n} (KC_ST({name})->{n})")
    o.append("#endif")
    return "\n".join(o) + "\n"


def main():
    check = "--check" in sys.argv[1:]
    want = {}
    blocks = find_blocks()
    if not blocks:
        fail("no blocks found")
    for ix, (rel, name, line, body) in enumerate(blocks, 1):   # (slot 0: knobi_core.c's own kc_st_core)
        defines, variables = parse_block(body, rel)
        if not variables:
            fail(f"{rel}: block {name}: no variables")
        want[f"{name}.h"] = header(rel, name, ix, defines, variables)
    want["list.h"] = "\n".join([
        "/* SPDX-License-Identifier: GPL-3.0-only */",
        "/* Written by tools/core_state.py: the per-instance state blocks (core/state/<block>.h), in knobi_core.c's order */",
        "#define KC_STATE_BLOCKS(X) " + " ".join(f"X({b[1]})" for b in blocks),
        f"#define KC_SLOTS {len(blocks) + 1}", ""])
    have = set(f for f in os.listdir(OUT) if f.endswith(".h")) if os.path.isdir(OUT) else set()
    stale = sorted(have - set(want))
    wrong = sorted(f for f in want if not os.path.exists(os.path.join(OUT, f)) or open(os.path.join(OUT, f)).read() != want[f])
    if check:
        if stale or wrong:
            fail("firmware/src/core/state/ is not what the firmware's blocks give (" + " ".join(wrong + stale) +
                 "): run python3 tools/core_state.py")
        print(f"core_state: {len(blocks)} blocks, {sum(len(parse_block(b[3], b[0])[1]) for b in blocks)} variables: "
              "headers current")
        return
    os.makedirs(OUT, exist_ok=True)
    for f in stale:
        os.remove(os.path.join(OUT, f))
    for f, text in want.items():
        with open(os.path.join(OUT, f), "w") as fh:
            fh.write(text)
    print(f"core_state: wrote {len(want)} headers in firmware/src/core/state/ ({len(blocks)} blocks)")


if __name__ == "__main__":
    main()
