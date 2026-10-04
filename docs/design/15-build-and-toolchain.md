# 15. Сборка и тулчейн (дизайн)

Статус: **черновик (на согласование)** · ревизия после ревью `oracle` (B1 + M1–M5 свёрнуты).
Область: как вендорится SDK, как собирается модуль win/linux, что публикуется. Требования —
`docs/06`, `docs/09`, `AR-017/018/019`, `NFR-PORT-001/002`, `docs/conventions/cpp-module.md`.

## 1. Контекст

Модуль обязан собираться из одного CMake на Windows (MSVC, Win32) и Linux (`-m32`) и грузиться
в HLDS/ReHLDS. Из-за `USE_METAMOD` (`AD-7`) тянем заголовки Metamod/HLSDK. Prior art — ReAPI,
AmxxEasyHttp, addtofullpack_manager, fastdl_mm. Ревью эмпирически подтвердило Linux-падение
на `min`/`max` из непатченного `extdll.h` (см. §2.9) и уточнило требования.

## 2. Решения

| # | Решение | Обоснование |
|---|---|---|
| 2.1 | **SDK вендорится копией** в `third_party/`: `amxx/` (`sdk/amxxmodule.{h,cpp}`, `moduleconfig.in.h`, `amxmodx_version.h`, `IGameConfigs.h`, `ITextParsers.h`, `amtl/`, `HLTypeConversion.h`), `metamod/`, `hlsdk/`. | Воспроизводимость без сети (OQ-9 закрыт: копия). |
| 2.2 | `amxxmodule.{h,cpp}` **не редактируются**; `moduleconfig.h` — наш файл, создаётся из `moduleconfig.in.h`. | Конвенция SDK. |
| 2.3 | **Вендоринг фиксирует upstream-источник и commit SHA**, не локальные `/tmp`-пути. Каждый снимок — в `third_party/VERSIONS.md` (repo, ref/commit, лицензия). | Воспроизводимость (M1). |
| 2.4 | C++17 (`REQUIRED ON`, `EXTENSIONS OFF`); `HAVE_STDINT_H`; `NOMINMAX`. | Единый стандарт. |
| 2.5 | **`USE_METAMOD` определяется только в `moduleconfig.h`** (не дублировать в compile-definitions). | Устранение redefinition warning/`-Werror` (M4). |
| 2.6 | `moduleconfig.h` лежит в **`src/`** (копия `moduleconfig.in.h`); `src/` — include-каталог. | Разрешает `#include "moduleconfig.h"` из amxxmodule.h (M3). |
| 2.7 | **Windows (MSVC, Win32):** `-A Win32`, `/MT` (static CRT, OQ-16), `ws2_32` (и опц. `wsock32`), `/EXPORT:GiveFnptrsToDll=_GiveFnptrsToDll@8,@1`, `/SECTION:.data,RW`. | HLDS 32-бит; экспорт из-за `USE_METAMOD` (canon AMXX). |
| 2.8 | **Linux (GCC, `-m32`):** `-static-libstdc++ -static-libgcc`, `-ffunction-sections -fdata-sections`, `-Wl,--gc-sections`, `-fcf-protection=none` (GCC≥8.1); линковка `dl m pthread`. | Старый libstdc++/glibc; CET. |
| 2.9 | **`hlsdk/extdll.h` патчится** (закомментировать макросы `min`/`max` на non-Windows). Патч фиксируется в `third_party/patches/` (или вендорится уже пропатченный форк). | **BLOCKER ревью:** непатченный `extdll.h` ломает Linux-сборку (`min`/`max` × libstdc++); `NOMINMAX` не помогает. Канон — AmxxEasyHttp (закомментировано). «SDK не редактируется» относится к `amxxmodule.*`, не к этому патчу. |
| 2.10 | Имена: `amxxrpc_amxx.dll` / `amxxrpc_amxx_i386.so`; `PREFIX ""`, `OUTPUT_NAME` по платформе. | Лоадер AMXX (`docs/06` §3). |
| 2.11 | CMake-структура: `src/`, `include/AmxxRpc.inc`, `examples/`, `third_party/`, корневой `CMakeLists.txt`, `cmake/`. | Единообразие. |
| 2.12 | CI: GitHub Actions — Linux (i386 multilib + `libc6-dev-i386`) + Windows (`-A Win32`); zip с layout `addons/amxmodx/...`. | `docs/06` §4. |

## 3. Vendoring: upstream-источники (fix M1)

| Каталог | Upstream | Ref (зафиксировать) |
|---|---|---|
| `third_party/amxx/` | `alliedmodders/amxmodx` (каталог `public/`) | commit SHA из целевой `1.10.5428` |
| `third_party/metamod/` | `alliedmodders/metamod-hl1` (или `metamod-r`) | commit/тег |
| `third_party/hlsdk/` | пропатченный форк (по образцу `Next21Team/AmxxEasyHttp` `dep/halflife`) **или** upstream + наш патч `min`/`max` | commit/тег |

- Локальные копии для немедленного вендоринга: `../amxx-dumper/amxmodx/public/`,
  `/tmp/amxx-research/{metamod-hl1/metamod,hlsdk}/`, `../amxx-dumper/dependencies/{hlsdk,metamod-am}/`.
- `third_party/VERSIONS.md` — источник, ref/commit, лицензия, дата.
- **Лицензии:** копировать upstream-лицензии в `third_party/<dep>/LICENSE*`; корневой `NOTICE`
  при необходимости (M2 плана).

## 4. CMake (эскиз, исправленный)

```cmake
cmake_minimum_required(VERSION 3.20)
# -m32 ДО project(): иначе ABI-проба CMake определит 64-bit (M5).
# Каноничнее — отдельный toolchain-файл cmake/linux-i686.cmake (по prior art).
if(NOT WIN32)
  foreach(f CMAKE_C_FLAGS CMAKE_CXX_FLAGS CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS)
    set(${f} "${${f}} -m32")
  endforeach()
endif()
project(amxxrpc CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(amxxrpc SHARED
  src/main.cpp
  third_party/amxx/sdk/amxxmodule.cpp)

target_include_directories(amxxrpc PRIVATE
  src
  third_party/amxx                    # IGameConfigs.h, ITextParsers.h
  third_party/amxx/sdk                # amxxmodule.h
  third_party/metamod                 # meta_api.h, osdep.h, dllapi.h, engine_t.h, plinfo.h, sdk_util.h
  third_party/hlsdk/common
  third_party/hlsdk/dlls              # extdll.h, cbase.h
  third_party/hlsdk/engine
  third_party/hlsdk/game_shared
  third_party/hlsdk/public)

# USE_METAMOD — ТОЛЬКО в moduleconfig.h (2.5); здесь его не задаём.
target_compile_definitions(amxxrpc PRIVATE HAVE_STDINT_H NOMINMAX)

if(MSVC)
  set_target_properties(amxxrpc PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded")  # /MT
  target_link_libraries(amxxrpc PRIVATE ws2_32)
  target_link_options(amxxrpc PRIVATE
    "/EXPORT:GiveFnptrsToDll=_GiveFnptrsToDll@8,@1" "/SECTION:.data,RW")
  set_target_properties(amxxrpc PROPERTIES OUTPUT_NAME amxxrpc_amxx)
else()
  target_compile_options(amxxrpc PRIVATE -ffunction-sections -fdata-sections -fcf-protection=none)
  target_link_libraries(amxxrpc PRIVATE dl m pthread)
  target_link_options(amxxrpc PRIVATE -static-libstdc++ -static-libgcc -Wl,--gc-sections)
  set_target_properties(amxxrpc PROPERTIES OUTPUT_NAME amxxrpc_amxx_i386)
endif()
set_target_properties(amxxrpc PROPERTIES PREFIX "")
```

> Include-каталоги в §4 подтверждены ревью (с патченным `extdll.h` компилируется). `pm_shared`
> не требуется для пустого модуля.

## 5. Загрузка модуля

- Активация — строка `amxxrpc` в `addons/amxmodx/configs/modules.ini`; Metamod обязателен (`AD-7`).
- Сборка кладёт `.dll`/`.so` в `addons/amxmodx/modules/`.

## 6. Верификация

- `cmake -S . -B build-win -A Win32 && cmake --build build-win --config Release` — 0 ошибок.
- `cmake -S . -B build-lin -DCMAKE_CXX_FLAGS=-m32 && cmake --build build-lin` — 0 ошибок.
- Модуль грузится в HLDS без ошибок; логирует имя/версию.
- `amxxpc 1.10.5428` (path фиксируется): `examples/AmxxRpcExample.sma` → 0/0 (он включает `Core.inc`).
  Прямая компиляция `.inc` даёт `error 013` (не проверка). Linux-`amxxpc` не читает `/mnt/*` —
  запускать из нативного ФС.
- CI-артефакт содержит точные пути `addons/amxmodx/...`.

## 7. Открытые вопросы

- OQ-16 (закрыт: `/MT`) — подтвердить на тест-сервере.
- Точные ref/commit SDK-снимков — `third_party/VERSIONS.md` (заполняется при вендоринге).
- Дополнительные флаги (`--version-script`, `-fvisibility=hidden`, `_GLIBCXX_USE_CXX11_ABI=0`) —
  по необходимости; не блокируют Этап 0.
