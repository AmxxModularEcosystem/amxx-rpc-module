# AmxxRpc

Транспортный C++-модуль AMX Mod X (Metamod-хук-модуль) для CS 1.6 (HLDS/ReHLDS): открывает
внешним инструментам JSON-RPC-транспорт (TCP loopback + токен) для команд, дебага, чтения
состояния и управления фейк-игроками. Парная поставка — публичный Pawn-инклюд регистрации методов.

> Статус: **Этап 0 (Bootstrap)** — каркас сборки и загружаемый пустой модуль. Транспорт/RPC/
> фейк-игроки — последующие этапы (см. `.omo/plans/0001-strategic-roadmap.md`).

## Документация

- ТЗ: [`docs/README.md`](docs/README.md)
- Дизайн: [`docs/design/`](docs/design/README.md) (системная архитектура — `design/10`, сборка — `design/15`)
- Журнал: [`docs/journal/`](docs/journal/README.md)
- Конвенции: [`AGENTS.md`](AGENTS.md), [`docs/conventions/`](docs/conventions/)

## Сборка

```
# Windows (MSVC, Win32)
cmake -S . -B build-win -A Win32 -DCMAKE_BUILD_TYPE=Release
cmake --build build-win --config Release

# Linux 32-bit
cmake -S . -B build-lin -DCMAKE_CXX_FLAGS=-m32 -DCMAKE_BUILD_TYPE=Release
cmake --build build-lin -j
```

Артефакты: `amxxrpc_amxx.dll` (Win) / `amxxrpc_amxx_i386.so` (Linux 32-bit).

## Установка

Скопировать модуль в `addons/amxmodx/modules/` и добавить строку `amxxrpc` в
`addons/amxmodx/configs/modules.ini`. Требуется Metamod.

## Инклюд

`include/AmxxRpc/Core.inc` → `addons/amxmodx/scripting/include/AmxxRpc/Core.inc`.
Подключение: `#include <AmxxRpc/Core>`. Пример — `examples/AmxxRpcExample.sma`.

## Vendored SDK

См. [`third_party/VERSIONS.md`](third_party/VERSIONS.md) и патч
[`third_party/patches/`](third_party/patches/). SDK (`amxxmodule.*`) не редактируется.
