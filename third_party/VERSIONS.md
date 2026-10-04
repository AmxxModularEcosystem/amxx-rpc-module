# Vendored third-party sources

| Path | Upstream repo | Commit | License |
|---|---|---|---|
| `third_party/amxx/` | https://github.com/alliedmodders/amxmodx (`public/`) | `4f0cb1e1210d8cae0f2edaf77cb2aec37012a656` | GPLv3 (see `third_party/amxx/licenses/`) |
| `third_party/metamod/` | https://github.com/alliedmodders/metamod-hl1 (`metamod/`) | `18a10db686702e8ae9e0fcb5b5febf8881dc9c2d` | GPL (see `third_party/metamod/GPL.txt` if present) |
| `third_party/hlsdk/` | https://github.com/alliedmodders/hlsdk (`common/ dlls/ engine/ game_shared/ public/ pm_shared/`) | `a0edb7792a96998d349325bebab8ea41ec5cb239` | HLSDK license (`third_party/hlsdk/LICENSE`) |

## Patches

| Patch | Target | Reason |
|---|---|---|
| `patches/hlsdk-extdll-minmax.patch` | `third_party/hlsdk/dlls/extdll.h` | comment out `min`/`max` macros (non-Windows) to avoid libstdc++ collision |

## Snapshot provenance

- AMXX snapshot taken from a local amxx-builder cache clone pinned to the commit above; the
  authoritative source is the upstream repo at that commit.
- The `moduleconfig.in.h` template is copied from AMXX `public/sdk/` and used to produce
  `src/moduleconfig.h` (our file; the SDK file itself is not edited).
