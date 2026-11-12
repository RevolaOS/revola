# Raw sites

For code no core site covers, the manifest may declare sites of its own. They are exclusive, checked against the running firmware at load, and limited to a pool of eight in the core. Ask for a core site when one would serve others too; first-party extensions use core sites only.

```toml
[[site]]
name = "coverflow_clear"     # a label for the site
address = 0x08123456         # the word to patch, from the pinned firmware
kind = "call"                # call, vtable or word
expected = 0xEB001234        # the word there now; the core refuses a mismatch
argc = 2                     # register arguments, at most 4; 0 for word
```

Bind the n-th declared site as `RV_RAW_SITE(n)` in a hook declaration. Only `call`, `vtable` and `word` with at most four register arguments and no stack arguments are allowed.

| Refusal in Settings > Extensions | Cause |
|---|---|
| Site mismatch | the address is outside the OSOS image below the heap, or does not hold the expected word |
| Conflict | the address lies inside a core site, which is named, or another extension's raw site |
| No memory | the pool of eight is used up |

A released site is reusable two seconds later. The core side is described in [Hook sites](https://github.com/RevolaOS/revola/blob/main/docs/core/hook-sites.md).
