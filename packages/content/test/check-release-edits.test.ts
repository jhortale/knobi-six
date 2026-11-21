import { spawnSync } from "node:child_process";
import { copyFileSync, mkdirSync, mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { describe, expect, it } from "vitest";

const script = join(import.meta.dirname, "../../../.github/scripts/check-release-edits.sh");
const fixture = join(import.meta.dirname, "fixtures/repo/synths/knobi-six/releases/1.0.md");

function git(cwd: string, ...a: string[]) {
  const r = spawnSync("git", ["-c", "user.email=a@b", "-c", "user.name=t", ...a], { cwd, encoding: "utf8" });
  if (r.status !== 0) throw new Error(r.stderr);
  return r.stdout.trim();
}

/** a repo with the release file committed; `edit` changes it in a second commit. Returns the run of the script. */
function check(edit: (text: string) => string, base?: string) {
  const d = mkdtempSync(join(tmpdir(), "edits-"));
  git(d, "init", "-q");
  mkdirSync(join(d, "synths/knobi-six/releases"), { recursive: true });
  const f = join(d, "synths/knobi-six/releases/1.0.md");
  copyFileSync(fixture, f);
  git(d, "add", "-A");
  git(d, "commit", "-qm", "base");
  const b = git(d, "rev-parse", "HEAD");
  writeFileSync(f, edit(readFileSync(f, "utf8")));
  git(d, "commit", "-qam", "edit");
  return spawnSync("sh", [script, base ?? b, "HEAD"], { cwd: d, encoding: "utf8" });
}

describe("check-release-edits.sh", () => {
  it("fails naming the file and the field when a CI-owned field changed", () => {
    const r = check((t) => t.replace("size: 2000", "size: 2001"));
    expect(r.status).toBe(1);
    expect(r.stdout).toContain("synths/knobi-six/releases/1.0.md");
    expect(r.stdout).toContain("builds");
  });
  it("passes when only the notes and current changed", () => {
    expect(check((t) => t.replace("First release", "Edited").replace("current: true", "current: false")).status).toBe(0);
  });
  it("fails, with a message, when the base cannot be diffed (never a silent pass)", () => {
    const r = check((t) => t.replace("size: 2000", "size: 2001"), "0123456789abcdef0123456789abcdef01234567");
    expect(r.status).not.toBe(0);
    expect(r.stderr).toContain("git diff");
  });
});
