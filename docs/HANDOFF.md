# Handing work to another AI agent

[AGENTS.md](../AGENTS.md) is the project contract and is loaded automatically by
agents working in this repository. It already covers architecture, save and
memory constraints, the test matrix, and hardware safety. **Do not restate it in
a prompt** - that wastes context and invites drift.

What an agent cannot infer is *this machine's* state and *your* authorization.
Preface with only that.

## Copy-paste template

```
Repo: pocket-tank-0.3.8 (CYD firmware). Read AGENTS.md first.

Environment on this machine:
- No gcc/clang on PATH. Use --cc zig (zig 0.17.0 is on PATH).
- ESP-IDF v6.1 at C:\esp\v6.1\esp-idf - run export.ps1 before build_cyd.ps1.
- Shell cwd may be firmware\ - use absolute paths for file edits.

Authorization for this task:
- Flash to COM4: yes / no
- Commit + push: yes / no
- Publish release/Pages: yes / no

Device state: <e.g. "COM4 runs 2.4.4, 5 residents, do not reset" | "no device attached">

What I want: <the actual request>
```

## The four things that matter

**1. Compiler.** There is no `gcc` or `clang` on this machine. `--cc zig` works
because zig is on PATH. Without a compiler argument the native runners fail with
`FileNotFoundError`, which looks like a broken test but is not - see the
invocation quirks in AGENTS.md "Build and validation".

**2. Authorization scope.** AGENTS.md treats source push, Pages deployment,
GitHub release, and physical-device flash as four separate operations. An agent
will correctly refuse to flash or publish without being told. State it up front
or you get a question instead of work.

**3. Device state.** Whether a board is attached, what it runs, and whether it is
safe to touch. Only you know this. AGENTS.md forbids manipulating a real
aquarium to stage screenshots, but it cannot know if COM4 is plugged in.

**4. Absolute paths.** The shell's working directory can be `firmware\`, so a
relative `tools/gen_icons.py` resolves to `firmware\tools\gen_icons.py` and
fails. Ask for absolute paths.

## What not to preface with

- **AGENTS.md content.** Already loaded.
- **Prior conversation.** Commit messages in this repo are detailed and carry the
  reasoning; `git log` is the better source.
- **"Be careful" / "test thoroughly".** AGENTS.md already mandates the matrix and
  forbids claiming untested success.
- **A codebase tour.** The architecture map is in AGENTS.md.

## Two habits that pay off

**Ask for verification, not assertion.** The failure mode here is not bad code,
it is confident claims. "Confirm the published hash matches its own
SHA256SUMS" gets evidence; "publish it" gets a claim.

**Name the scope boundary.** "Refactor `render.c` only" or "fix the flake, do not
touch the test matrix" stops an agent from helpfully expanding into adjacent
systems.

## Verified environment facts

Checked on 2026-11-04. Re-verify rather than trusting this list if the machine
may have changed.

| Tool | State |
| --- | --- |
| Python | 3.12.10 on PATH |
| zig | 0.17.0 on PATH (a second 0.16.0 exists in Python site-packages; both work) |
| gcc / clang | **not installed** |
| Node.js | **not installed** - the `tools/test_*.cjs` and `test_usb_updater.mjs` browser suites cannot run locally; CI runs them |
| ESP-IDF | v6.1 at `C:\esp\v6.1\esp-idf`; `export.ps1` provides ninja, cmake, idf.py and the xtensa toolchain |
| cJSON | present at `firmware/managed_components/espressif__cjson` (gitignored, needed by native builds) |
| `.browser-tools/` | present with emsdk, browsers and node_modules (gitignored) |

## Quick sanity check before asking for work

```powershell
cd C:\Users\Jerome\Downloads\pocket-tank-0.3.8
git status -sb
git log --oneline -3
python tools\test_wardrobe.py --cc zig
```

A clean tree, a recent commit, and one passing native runner confirm the
environment is usable before you spend a prompt on the real request.
