# Hook sites

Apple code runs from DRAM, so the core patches it at boot. `core/hooks/sites.def` lists every site the core can patch. Each site has one pointer slot and one stub in the core. Extensions bind handlers to sites by id.

## Site rows

A row gives the address, the kind, what an unbound site does, the Apple words expected there, whether several handlers may chain, and the argument count.

| Kind | Patches |
|---|---|
| `CALL` | a `BL` or register `BLX` |
| `VTABLE` | a pointer in a vtable or table |
| `WORD` | a constant |
| `INSERT` | a call written over an ordinary instruction; the stub preserves r0-r3 and r12 |
| `ENTRY` | the first two words of a function |
| `ENTRY_NORETURN` | as `ENTRY`, for a function that does not return |

`CALL`, `VTABLE`, `WORD` and `INSERT` sites are patched when the first handler binds and restored when the last unbinds. Entry sites are written once by `core_boot` and fall through to a replay stub while unbound.

`core_boot` runs inside Apple's UI task after the other tasks exist, so a two-word entry write is only safe for a function no started task can be executing at that moment. A site is declared `ENTRY` only with that argument. Every write runs with IRQ and FIQ masked and is followed by `core_cache_sync`.

A row's `COPY(n, r)` terms make a value Apple keeps in a callee-saved register available as site argument `n`, for a call whose callee takes fewer arguments than the handler needs. When all four argument registers are live, `PUSH(r)` pushes the register after Apple's two stack arguments instead (`PUSH` goes with `STACK(2)`), so the handler receives it as its seventh value and passes it on with `hook_next7`. `PUSH` is for a handler that needs the register before and after the call it wraps; an observer that only records the register ahead of the call is an `INSERT` row and needs no stub term.

## Handlers

A handler receives the Apple arguments followed by `next`: in `r[argc]`, or as the first stack argument for four-argument sites, ahead of up to two arguments Apple itself passes on the stack and a `PUSH` register.

| Handler | Behaviour |
|---|---|
| filter | continues with `core_next_call`; several may chain in priority order |
| replace | exclusive; a second claim is reported as a conflict |

With one handler bound the slot holds the handler itself. With more it holds a chain entry that calls them in priority order. Handler lists are published with a single store, and the retired list is freed at the next grace tick.

## Site ids

Extensions name a site by id, the FNV-1a hash of its `sites.def` row name. The build writes the table of ids next to the site table and the same values into the SDK's `rv_sites.h`, so a row may move without breaking built extensions.

## Raw sites

An extension's manifest may declare sites of its own, bound as `RV_RAW_SITE(n)`; see [Raw sites](../sdk/raw-sites.md) for the manifest side. The loader claims them before entering the main module.

| Check | Refusal left on the record |
|---|---|
| address in the OSOS image below the heap, holding the expected word | `Site mismatch` |
| not inside a core site or another extension's raw site | `Conflict`, naming the core site when one covers the address |
| a free slot in the pool | `No memory` |

The core keeps a pool of eight raw sites after the core rows. Each has a 36-byte stub in the core's bss, written at the claim, that holds its own slot and root continuation and records its owner for the panic attribution. A released site is reusable two grace ticks later. First-party extensions use core sites only.

## Adding a site

1. Find the function and the patch site in Ghidra. Name them and set the prototype there.
2. Add them to `reversing/symbols/osos-2.0.5/<subsystem>.toml` (`[functions]`, `[sites]`). Run `symbols.py refresh` and `push` against the project, then `generate`, `check` and `verify`. Commit in reversing.
3. Copy `osos-2.0.5.{h,ld}` into `firmware/` unchanged (`cmp` must show them identical).
4. Add the `sites.def` row using `SITE_<name>` and `ADDR_<callee>`.

## Grace tick

One native UI event timer (`core/timer.c`) fires every second on the UI task. The tick frees retired handler lists and extension images, unloads what was disabled, ages idle on-demand modules and writes a dirty settings store once the disk is powered anyway. The recipient is a core object whose vtable has only the event slot; the timer re-arms itself on every delivery.
