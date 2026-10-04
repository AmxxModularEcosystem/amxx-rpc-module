# 06. Сборка, CI и раскладка артефактов

## 1. CMake

- Корневой `CMakeLists.txt`; исходники — `src/`; вендоренный SDK — `sdk/`
  (`public/sdk/*`, `public/amxmodx_version.h`, `public/IGameConfigs.h`, `public/ITextParsers.h`,
  AMTL), Metamod/HLSDK-заголовки — `sdk/metamod/`, `sdk/hlsdk/` (нужны из-за `USE_METAMOD`).
- Публичный инклюд — `include/AmxxRpc/Core.inc`; пример плагина — `examples/` (`.sma`).

## 2. Компиляция/линковка (подтверждено prior art)

- C++17; `HAVE_STDINT_H`; `USE_METAMOD` (в `moduleconfig.h`).
- **Linux:** `-m32` (multilib), `-static-libstdc++ -static-libgcc`, `-fcf-protection=none`
  (GCC ≥ 8.3), `-Wl,--gc-sections`; суффикс `_amxx_i386.so`; линковать `dl m pthread`.
- **Windows (MSVC, Win32):** `-A Win32` (не x64!), `ws2_32` + `wsock32`,
  `/EXPORT:GiveFnptrsToDll=_GiveFnptrsToDll@8,@1` и `/SECTION:.data,RW` (из-за `USE_METAMOD`),
  суффикс `_amxx.dll`. `WSAStartup`/`WSACleanup`.
- Итоговое имя: `amxxrpc_amxx.dll` / `amxxrpc_amxx_i386.so`.

## 3. Раскладка артефактов

```
addons/amxmodx/modules/amxxrpc_amxx.dll        (Win)
addons/amxmodx/modules/amxxrpc_amxx_i386.so    (Linux)
addons/amxmodx/scripting/include/AmxxRpc/Core.inc
addons/amxmodx/configs/amxxrpc.cfg              (пример)
```
- Активация: строка `amxxrpc` в `addons/amxmodx/configs/modules.ini`.

## 4. CI (GitHub Actions)

- Матрица: Linux (Ubuntu + `dpkg --add-architecture i386`, `gcc-multilib g++-multilib`)
  → `.so`; Windows (`windows-latest`, `cmake -A Win32`) → `.dll`.
- Публикация артефактов в layout `addons/amxmodx/...` (zip), как в prior art.
- Проверка совместимости glibc/ABI на Linux (по образцу ReAPI) — опционально.

## 5. Верификация (команды этапа)

- Сборка CMake win/linux — **0 ошибок**.
- Загрузка модуля в HLDS без ошибок в логе.
- Smoke JSON-RPC по TCP: `rpc.auth` + `rpc.ping`/`rpc.version` → осмысленный ответ.
- `amxxpc 1.10.5428` для примера-плагина `examples/AmxxRpcExample.sma` (он включает
  `include/AmxxRpc/Core.inc`) — **0/0**. Прямая компиляция `.inc` не приводится к 0/0
  (`error 013: no entry point`) и проверкой не является.
- **Env-ловушка (Linux/WSL):** `amxxpc` — 32-битный и **не читает файлы на DrvFs/9p-монтированиях
  (`/mnt/*`)**; при нечитаемом исходнике падает `std::bad_alloc` (а не печатает диагностику).
  Компиляцию запускать из Linux-нативного каталога (напр. `/tmp`, `~/`).
- Точные команды фиксируются в плане конкретного этапа.

## 6. Интеграция с amxb-бандлом

- Сборка модуля — CMake (вне amxb). Поставка модуля/инклюда в рантайм-бандл — на стороне
  `amxb` (манифест бандла), т.к. amxb пакует серверные артефакты.
- Модуль и инклюд не должны требовать доступа amxb к процессу сборки C++.

## 7. Открытые детали (в дизайн-док)

- Точная стратегия вендоринга SDK: копия в `sdk/` vs git-submodule vs FetchContent.
- Нужен ли CMake-хелпер `set_output_binary_name` (по образцу prior art).
- Версии инструментов/раннеров CI.
