# AmxxRpc

Транспортный C++-модуль AMX Mod X (Metamod-хук-модуль) для CS 1.6 (HLDS/ReHLDS): открывает
внешним инструментам **JSON-RPC 2.0**-транспорт (TCP loopback + токен) для команд, дебага, чтения
состояния и управления **фейк-игроками**. Парная поставка — публичный Pawn-инклюд регистрации методов.

> Статус: **реализован** (этапы 0–4; см. `docs/journal/` и `.omo/plans/0001-strategic-roadmap.md`).
> Модуль — чистый транспорт; MCP-мост и каталог tools живут отдельно (в `amxb`).

## Возможности

- **Транспорт**: TCP `127.0.0.1` + токен; JSON-RPC 2.0 (newline-framed); `rpc.auth/ping/version/methods`.
- **Ядро**: `server.exec`, `cvar.get/set`, `players.list/get`, `events.subscribe/unsubscribe`.
- **Pawn-API**: `ARpc_Core_RegisterMethod`/`Reply`/`ReplyError`/`Emit` — плагины публикуют свои методы.
- **Фейк-игроки** (`fake.*`): create/remove/list/get/move/look/stop/buttons/set/authid; подмена authid;
  отказ на реальных игроках; лимит `fake_max`.
- **Боты** (`bot.*`, опц.): навигация через YAPB, если он установлен; иначе — деградация (`-32002`).

## Сборка

```
# Windows (MSVC, Win32) — рекомендуемый путь
cmake -S . -B build-win -A Win32 -DCMAKE_BUILD_TYPE=Release
cmake --build build-win --config Release

# Linux 32-bit
cmake -S . -B build-lin -DCMAKE_CXX_FLAGS=-m32 -DCMAKE_BUILD_TYPE=Release
cmake --build build-lin -j

# Установка раскладки addons/amxmodx/... в <prefix>:
cmake --install build-win --prefix dist     # → dist/addons/amxmodx/{modules,scripting/include,configs}
```

Артефакты: `amxxrpc_amxx.dll` (Win) / `amxxrpc_amxx_i386.so` (Linux 32-bit).

> **Кросс-сборка без MSVC** (например, в WSL через zig): `zig c++ -target x86-windows-gnu -shared …
> cmake/amxxrpc.def` (`.def` даёт undecorated-экспорт `GiveFnptrsToDll`, нужный AMXX/Metamod).
> Штатная MSVC-сборка делает это через `/EXPORT`.

## Установка

1. Скопировать модуль в `addons/amxmodx/modules/`.
2. Добавить строку `amxxrpc` в `addons/amxmodx/configs/modules.ini`.
3. Создать `addons/amxmodx/configs/amxxrpc.cfg` (пример — [`configs/amxxrpc.cfg`](configs/amxxrpc.cfg));
   обязателен `token` (≥16 символов).
Требуется Metamod.

## Инклюд

`include/AmxxRpc.inc` → `addons/amxmodx/scripting/include/AmxxRpc.inc`.
Подключение: `#include <AmxxRpc>`. Пример — [`examples/AmxxRpcExample.sma`](examples/AmxxRpcExample.sma).

## Проверка

- Unit-тесты (off-line, без AMXX): `tests/protocol_test.cpp`, `tests/registry_test.cpp`, `tests/bot_test.cpp`.
- E2E (Windows-Python против локального HLDS): [`scripts/e2e_all.py`](scripts/e2e_all.py)
  (сценарии: `smoke_tcp.py`, `e2e_fake.py`, `e2e_bot.py`).

## Документация

- ТЗ: [`docs/README.md`](docs/README.md)
- Дизайн: [`docs/design/`](docs/design/README.md) (архитектура — `design/10`, транспорт — `11`,
  реестр/Pawn — `12`, фейк-игроки — `13`, YAPB — `14`, сборка — `15`, конфиг — `16`)
- Журнал: [`docs/journal/`](docs/journal/README.md)
- Конвенции: [`AGENTS.md`](AGENTS.md), [`docs/conventions/`](docs/conventions/)
- Зависимости/вендоринг: [`docs/09`](docs/09-dependencies.md), [`third_party/VERSIONS.md`](third_party/VERSIONS.md)
