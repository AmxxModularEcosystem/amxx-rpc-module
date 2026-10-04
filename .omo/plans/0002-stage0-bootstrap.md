# Work Plan: Этап 0 — Bootstrap (AmxxRpc)

## Profile bindings

- Ревьюер дизайна: `oracle`. Ревьюер плана: `momus`.
- Дизайн-гейт этапа: [`docs/design/15-build-and-toolchain.md`](../../docs/design/15-build-and-toolchain.md).
- **Гейт реализации:** реализация НЕ начинается, пока дизайн (`oracle`) и план (`momus`) не
  пройдены; `design/15` перевести из «черновик» в утверждённый.
- Планы: `.omo/plans/`. Журнал: `docs/journal/` (запись `0002`). OQ: `docs/08` §1/§3.
- Верификация: см. §Verification. Политика коммитов: только по явному указанию владельца.
- Закрывает OQ: OQ-9 (вендоринг копией), OQ-16 (`/MT`).

## Goal

Создать каркас репозитория и **загружаемый пустой модуль** AmxxRpc: вендоренный SDK, CMake
(win/linux), CI, `moduleconfig.h` с `USE_METAMOD`, `OnAmxxAttach/Detach` с логированием версии,
публичный инклюд с версией и пример-плагин. Без транспорта/RPC/фейков.

## Source of truth

- `docs/design/15`, `docs/06`, `docs/09`, `docs/conventions/cpp-module.md`, `docs/conventions/amxx-pawn.md`,
  `docs/design/10`, `AGENTS.md`.

## Deliverables

| Артефакт | Что |
|---|---|
| `third_party/amxx/` | Копия `sdk/{amxxmodule.h,amxxmodule.cpp,moduleconfig.in.h}`, `amxmodx_version.h`, `IGameConfigs.h`, `ITextParsers.h`, `amtl/`, `HLTypeConversion.h` |
| `third_party/metamod/`, `third_party/hlsdk/` | Заголовки Metamod/HLSDK; `hlsdk/extdll.h` **пропатчен** (min/max) — патч в `third_party/patches/` |
| `third_party/VERSIONS.md` | Upstream repo + ref/commit + лицензия по каждому снимку |
| `third_party/<dep>/LICENSE*`, корневой `NOTICE` | Лицензии вендоренных SDK |
| `src/moduleconfig.h` | Из шаблона: `MODULE_*`, `USE_METAMOD` (единственное место), `FN_AMXX_ATTACH/DETACH` |
| `src/main.cpp` | `OnAmxxAttach`/`OnAmxxDetach`, логирование `MODULE_NAME`/`MODULE_VERSION` |
| `CMakeLists.txt`, `cmake/` | Сборка win/linux (флаги `design/15` §4) |
| `include/AmxxRpc/Core.inc` | Инклюд: `#define ARP_VERSION`, include-guard, каркас нативов |
| `examples/AmxxRpcExample.sma` | Пустой пример плагина |
| `.github/workflows/build.yml` | CI-матрица Linux(i386)+Windows(Win32), артефакт-zip |
| `.gitignore` | Исключить `build*/`, `*.amxx` |
| `README.md` | Краткое описание + сборка |

## Steps (ordered)

1. **Предусловие — `amxxpc 1.10.5428`** — получить компилятор (из пакета AMXX 1.10 либо сборкой
   из `amxxpc`-исходников), зафиксировать абсолютный путь; команды верификации использовать с ним.
2. **git init** — инициализировать репозиторий (сейчас нет `.git`); добавить `.gitignore`
   (`build*/`, `*.amxx`, `third_party/**/build/`).
3. **SDK vendoring** — скопировать `third_party/{amxx,metamod,hlsdk}`; `hlsdk/extdll.h` пропатчить
   (min/max) и сохранить патч в `third_party/patches/`; скопировать лицензии; заполнить
   `VERSIONS.md` (upstream repo, ref/commit, лицензия).
4. **moduleconfig.h** — из `moduleconfig.in.h` в `src/`; `USE_METAMOD` (только здесь),
   `FN_AMXX_ATTACH/DETACH`, `MODULE_* = AmxxRpc`.
5. **main.cpp** — `OnAmxxAttach` (лог версии; каркас подсистем), `OnAmxxDetach` (заглушка).
6. **Core.inc** — `#define ARP_VERSION "0.0.1"`, include-guard, заготовки нативов (без реализации).
7. **examples/** — пустой плагин, включающий `AmxxRpc/Core`.
8. **CMake** — сборка модуля; флаги/экспорт по платформам (`design/15` §4); имя `amxxrpc_amxx[_i386]`.
9. **CI** — workflow win/linux + упаковка `addons/amxmodx/{modules,scripting/include/AmxxRpc}`.
10. **README** — описание, сборка, активация в `modules.ini`.
11. **Record** — журнал `0002` + индекс; перенести OQ-9/OQ-16 в `docs/08` §1 «Принятые решения»;
    перевести `design/15` в утверждённый в `docs/design/README.md`; обновить `VERSIONS.md`.

## Acceptance criteria (verifiable)

- `amxxrpc_amxx.dll` и `amxxrpc_amxx_i386.so` собираются **0 ошибок** (win+linux).
- Модуль грузится в HLDS без ошибок; в логе — имя/версия `AmxxRpc`.
- `include/AmxxRpc/Core.inc` проверяется **через пример** `examples/AmxxRpcExample.sma` —
  `amxxpc 1.10.5428` **0/0**. (Прямая компиляция `.inc` даёт ожидаемую `error 013: no entry
  point`; это не проверка. Linux-`amxxpc` не читает `/mnt/*` — компилировать из нативного ФС.)
- Строка `amxxrpc` в `modules.ini` загружает модуль.
- Архив CI содержит точные пути `addons/amxmodx/...`.

## Verification (точные команды)

```
# Windows (MSVC)
cmake -S . -B build-win -A Win32 -DCMAKE_BUILD_TYPE=Release
cmake --build build-win --config Release

# Linux 32-bit
cmake -S . -B build-lin -DCMAKE_CXX_FLAGS=-m32 -DCMAKE_BUILD_TYPE=Release
cmake --build build-lin -j

# Плагины/инклюд (проверка инклюда — через пример; Linux-amxxpc не читает /mnt/*)
# Скопировать include/ + examples/ на нативный ФС (напр. /tmp) и запустить оттуда:
amxxpc examples/AmxxRpcExample.sma -o/tmp/AmxxRpcExample.amxx -iinclude -i<amxx-include-dir>

# Загрузка
# (тест-сервер) скопировать .so в addons/amxmodx/modules/, в modules.ini добавить "amxxrpc",
# проверить лог: "AmxxRpc vX.Y.Z loaded"
```

## MUST NOT

- Реализовывать транспорт/RPC/фейки/ботов (следующие этапы).
- Редактировать `amxxmodule.{h,cpp}` (только `moduleconfig.h`).
- Логировать секреты; вводить обязательные рантайм-зависимости.
- Обращаться к AMXX/движку из не-главного потока (в этом этапе потоков ещё нет).

## Out of scope

- Транспорт (`design/11`), реестр/Pawn (`design/12`), фейки (`design/13`), боты (`design/14`),
  конфиг (`design/16`) — последующие этапы.

## Risks / notes

- Экспорт `GiveFnptrsToDll` и `/MT` на MSVC; совместимость glibc на Linux.
- Точные пути includes для Metamod/HLSDK при `USE_METAMOD` (уточняются при вендоринге).
- Загрузка в HLDS требует тест-сервера (AS-1).
