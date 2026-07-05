# HagIPC

A tiny **local debug / IPC server** that runs inside Skyrim SE (1.6.1170) as an SKSE
plugin. It lets an external client (a script, a REPL, an AI agent) inspect and drive
the running game over a **localhost TCP** socket — built to accelerate the manual
reverse-engineering / mod-development work for the Hagryph mod suite.

> ⚠️ **DEV TOOL — treat it like an attached debugger.** It exposes raw memory access,
> arbitrary native calls, code execution and the in-game console. It binds to
> `127.0.0.1` only and never to a public interface, but you should **not enable it on a
> machine you don't trust, and never ship it enabled to end users.** Disable it with
> `Enabled = false` in the config when you're done.

## Config

First run writes `Documents/My Games/Skyrim Special Edition/SKSE/HagIPC.ini`:

```ini
[HagIPC]
Enabled = true     ; turn the server on/off
Port    = 19000    ; localhost TCP port
Token   =          ; optional shared secret (if set: first line must be `auth <token>`)
```

This config is **global** (not tied to a save) — the eventual shared `HagConfig`
library will manage per-character config that rides in the game co-save.

## Protocol (v0.2)

Line-based text over TCP, one client at a time. Offsets are **RVAs off the PE image
base `0x140000000`** (or a raw runtime VA if prefixed `abs:`) — the server adds the live
module base, so the client never needs to derive it. Each response is a single line:
`ok <...>` or `err <message>`.

| Command | Meaning |
|---------|---------|
| `ping` | → `ok pong` |
| `base` | → `ok 0x7ff6…` (live module base) |
| `read <off> <type> [c0 c1 …]` | follow a pointer chain (`p = *p + c` per offset), read a typed value |
| `readb <off> <len> [c0 c1 …]` | read `len` raw bytes (hex) |
| `write <off> <type> <val> [c0 …]` | write a typed value (main thread) |
| `call <off> [a0 … a7]` | call a game function with ≤8 integer/pointer args → `ok 0x<rax>` (main thread) |
| `exec <hexblob>` | run a machine-code blob (entry@0, ends `ret`, MS x64) → `ok 0x<rax>` (main thread) |
| `console <text>` | run an in-game console command *(pending — next patch)* |
| `help` | list commands |

Types: `u8 u16 u32 u64 i8 i16 i32 i64 f32 f64 ptr`.

`write`/`call`/`exec`/`console` are **marshaled onto the game main thread** via the SKSE
Task interface (the socket thread blocks until the task runs, ~1 frame). `read`/`readb`
run on the socket thread (SEH-guarded). `call` args are 64-bit ints only (no float args,
return is RAX); use an `exec` thunk for float ABIs.

**Examples** (e.g. the UI singleton pointer lives at RVA `0x20F8958`):

```
read 0x20F8958 ptr           ; → ok <UI*> 0x… @0x…
read 0x20F8958 u32 0x120     ; deref UI*, read u32 at +0x120 (open-menu count)
readb 0x20F8958 16 0x0       ; deref UI*, dump 16 bytes
```

Reads are SEH-guarded: a bad offset/chain returns `err access violation` instead of
crashing the game.

## Roadmap

- ✅ `read`/`readb` (SEH-guarded, off-thread), `write`/`call`/`exec` (main-thread via the
  SKSE Task interface).
- `console <text>` — the Script build/compile/run chain is RE'd (`Script::CompileAndRun`
  @ RVA `0x33d6a0`, selected-ref resolver `0x1795f0`); wiring the Script creation is next.
- `HagConfig.dll` — a shared config library any mod can use for per-character (co-save)
  and global (per-mod file) settings.

## Build

`./build.ps1` (CMake + vcpkg, MSVC x64) → `HagIPC.dll`, deployed into the MO2 mod
`HagIPC/SKSE/Plugins/HagIPC.dll`.
