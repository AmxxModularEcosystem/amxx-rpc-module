# 0002. Этап 0 — Bootstrap (каркас, SDK, CMake, пустой модуль)

**Дата:** 2026-10-04 · **Этап:** 0 Bootstrap · **Статус:** реализовано; Linux-модуль собран,
Pawn 0/0; CMake/HLDS/Windows — отложены (см. «Открытые нити»)

## Контекст

Первый этап дорожной карты (`.omo/plans/0001`) после валидации стратегии. Цель — каркас сборки
и **загружаемый пустой модуль** без транспорта/RPC/фейков. Дизайн-гейт — `docs/design/15`
(провалидирован `oracle`: блокер B1 по `min`/`max` и M1–M5 свёрнуты); план — `.omo/plans/0002`.

## Сделано

- **Вендоринг SDK** (копией) в `third_party/`:
  - `amxx/` ← `alliedmodders/amxmodx` `public/` @ `4f0cb1e1…` (sdk, amtl, IGameConfigs/ITextParsers,
    amxmodx_version, HLTypeConversion, licenses);
  - `metamod/` ← `metamod-hl1` @ `18a10db6…`;
  - `hlsdk/` ← `hlsdk` @ `a0edb779…` (common/dlls/engine/game_shared/public/pm_shared).
  - Провенанс — `third_party/VERSIONS.md`; лицензии скопированы; корневой `NOTICE`.
- **Патч `hlsdk/dlls/extdll.h`** — закомментированы `min`/`max` (non-Windows); патч записан в
  `third_party/patches/hlsdk-extdll-minmax.patch` (BLOCKER B1 из ревью).
- **Исходники:** `src/moduleconfig.h` (`USE_METAMOD` только здесь; `FN_AMXX_ATTACH/DETACH`),
  `src/main.cpp` (`OnAmxxAttach/Detach` + лог версии), `include/AmxxRpc/Core.inc` (`ARP_VERSION`),
  `examples/AmxxRpcExample.sma`.
- **Сборка:** `CMakeLists.txt` (win MSVC `/MT`+`/EXPORT`+`/SECTION:.data,RW`; linux `-m32`+
  `-static-libstdc++`+`-fcf-protection=none`; имена `amxxrpc_amxx[_i386]`), `.github/workflows/build.yml`
  (матрица linux/windows, артефакт-zip), `.gitignore`, `README.md`.
- **git init** (репозиторий ранее не был git).

## Решения и обоснование

- **Вендоринг — копией** (OQ-9 закрыт): воспроизводимость без сети; upstream+commit в VERSIONS.md (M1).
- **`hlsdk/extdll.h` патчится** (B1): иначе Linux-сборка падает конфликтом `min`/`max` × libstdc++;
  «SDK не редактируется» относится к `amxxmodule.*`, а не к этому патчу (`design/15` §2.9).
- **`USE_METAMOD` — только в `moduleconfig.h`** (M4); `-m32` выставляется **до `project()`** (M5).
- **OQ-16 закрыт:** MSVC `/MT` (static CRT).
- Инклюд-каталоги и CMake-эскиз — по `design/15` §4 (подтверждены компиляцией).

## Верификация (доказательства)

- **Linux-модуль:** собран 32-битным `g++ -m32` с флагами Linux-ветки CMake → `amxxrpc_amxx_i386.so`
  (ELF 32-bit, exit 0). Экспорты ABI подтверждены `nm -D`: `AMXX_Query/Attach/Detach`,
  `Meta_Query/Attach`, `GiveFnptrsToDll`.
- **Pawn:** `amxxpc 1.10.5428` → `examples/AmxxRpcExample.sma` (включает `AmxxRpc/Core`) — **0/0**.
- **Env-факты:** (1) `cmake` в окружении отсутствует (sudo без TTY — установка невозможна) →
  CMake-сборка локально не запускалась, проверена эквивалентной прямой компиляцией; (2) Linux-`amxxpc`
  **не читает `/mnt/*`** (WSL DrvFs) и падает `bad_alloc` — компиляция запускалась из `/tmp`;
  (3) прямая компиляция `.inc` даёт `error 013: no entry point` — инклюд проверяется через пример.

## Открытые нити

- **CMake-сборка не запускалась** (нет `cmake`): подтвердить на CI/окружении с CMake.
- **Windows-сборка и загрузка в HLDS** не проверены (нет MSVC/HLDS локально) — при первой возможности.
- `MODULE_RELOAD_ON_MAPCHANGE` закомментирован (модуль не перезагружается на смену карты) —
  осознанное решение для сохранения транспорта, финально подтвердить на этапе транспорта.
- CI-workflow не запускался (нет push).

## Затронутые артефакты

- `third_party/{amxx,metamod,hlsdk,patches,VERSIONS.md}`, `NOTICE`, `src/{moduleconfig.h,main.cpp}`,
  `include/AmxxRpc/Core.inc`, `examples/AmxxRpcExample.sma`, `CMakeLists.txt`, `.github/workflows/build.yml`,
  `.gitignore`, `README.md`; правки `docs/design/15`, `docs/06`, `.omo/plans/0001`, `.omo/plans/0002`.

## Дальше

- **Этап 1 — Транспорт** (`docs/design/11`): TCP loopback, фрейминг, auth, I/O-поток + очереди +
  `StartFrame`-дренаж, конфиг (`docs/design/16`). Перед реализацией — гейт ревью дизайна/плана этапа.
