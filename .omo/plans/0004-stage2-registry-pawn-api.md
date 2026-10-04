# Work Plan: Этап 2 — Реестр методов и Pawn-API (AmxxRpc)

## Profile bindings

- Ревьюер дизайна: `oracle`. Ревьюер плана: `momus`.
- Дизайн-гейт: [`docs/design/12-rpc-registry-and-pawn-api.md`](../../docs/design/12-rpc-registry-and-pawn-api.md).
- Планы: `.omo/plans/`. Журнал: `docs/journal/` (запись `0004`).
- **Гейт реализации:** после ревью дизайна и плана; `design/12` → утверждён.
- Коммит — владелец разрешил коммитить каждый этап.
- Закрывает OQ: OQ-2, OQ-3, OQ-10, OQ-13, OQ-14.

## Goal

Реестр RPC-методов + диспетчер; нативы `ARpc_Core_*` для регистрации/ответа; встроенное ядро
(`server.exec`, `cvar.get/set`, `players.list/get`, `events.subscribe/unsubscribe`, `rpc.methods`);
события connect/disconnect/map + `Emit`; пример-плагин с методом.

## Deliverables

| Артефакт | Что |
|---|---|
| `src/RpcRegistry.{h,cpp}` | реестр (builtin/pawn/transport), коллизии, метаданные |
| `src/CoreMethods.{h,cpp}` | server.exec, cvar.*, players.*, rpc.methods |
| `src/Events.{h,cpp}` | подписки (main), push, ленивая прунинг, `Emit` |
| `src/PawnApi.{h,cpp}` | нативы `ARpc_Core_*` (регистрация через **`MF_AddNatives`/`AMX_NATIVE_INFO`** в `OnAmxxAttach`), ownership-check, SP-forward dispatch |
| `src/RpcDispatch.cpp` | рефактор: `Reply(handle,…)`/`ReplyError(handle,…)`, `Outstanding{methodName,pluginId}`, реестр |
| `src/Transport.{h,cpp}` | + `Transport_AuthenticatedCount()` |
| `src/moduleconfig.h` | + `FN_ClientConnect/Disconnect/ServerActivate`, `FN_AMXX_PLUGINSUNLOADING` |
| `include/AmxxRpc/Core.inc` | нативы + `ARP_ERR_*` + `ARP_PROTO_VERSION` |
| `examples/AmxxRpcExample.sma` | регистрирует `demo.echo` |
| `tests/registry_test.cpp` | off-line тест реестра (коллизии, override, метаданные) |
| **`CMakeLists.txt`** | добавить новые `.cpp` (RpcRegistry/CoreMethods/Events/PawnApi) + таргет `registry_test` |
| `docs/01` | FR-RPC-005/008/009 (проверить/подтвердить — уже обновлены) |
| `docs/03`, `docs/08` | синк требований/OQ под решения Этапа 2 (см. §12) |
| `docs/journal/0004-registry-and-pawn-api.md` | запись |

## Steps (ordered)

1. **Рефактор Этапа 1:** `Outstanding{methodName,pluginId}`; `Rpc_Reply/Rpc_ReplyError(handle,…)`
   (поиск `sessionId`); `DispatchMethod` → реестр; `rpc.methods` в новый формат `[{name,source,description}]`.
2. **RpcRegistry** — регистрация builtin/pawn/transport, коллизии (Pawn dup = ошибка; core-override запрет),
   каталог; отделить от AMXX для тестируемости (интерфейс forward-регистратора).
3. **PawnApi** — нативы, **регистрируемые через `MF_AddNatives` (таблица `AMX_NATIVE_INFO`) в
   `OnAmxxAttach`**; C-обработчики, не Pawn-`@` (уточнение: `@ARpc_*` — это Pawn-обработчики плагина,
   не нативы модуля). Нативы: Register/Unregister/Reply/ReplyError/Emit/IsConnected/GetVersion;
   ownership-check (pluginId через `MF_FindScriptByAmx`); `MF_RegisterSPForwardByName(MF_GetScriptAmx(pluginId),
   callback, FP_CELL, FP_STRING, FP_DONE)` (проверка `-1`); dispatch `MF_ExecuteForward(forwardId, handle, paramsJson)`.
4. **CoreMethods** — `server.exec` (`{ok:true}`), `cvar.get/set`, `players.list/get`, **`events.subscribe`/
   `events.unsubscribe`**, `rpc.methods` (формат — **голый массив** `[{name,source,description}]`);
   валидация JSON ответов.
5. **Events** — карта подписок (main), `player_connect/disconnect` (`FN_ClientConnect/Disconnect`),
   `map_start` (`FN_ServerActivate`), `Emit`, ленивая прунинг; `Transport_AuthenticatedCount`.
6. **moduleconfig.h** — добавить хуки (ClientConnect/Disconnect, ServerActivate, PLUGINSUNLOADING).
7. **Core.inc** — нативы + константы; обновить `ARP_VERSION`.
8. **Example** — `demo.echo` через `ARpc_Core_RegisterMethod`/`Reply`.
9. **Очистка на выгрузке** — `PLUGINSUNLOADING`: `MF_UnregisterSPForward` для всех + fail pending.
10. **tests/registry_test.cpp** — off-line (без AMXX).
11. **CMake** — добавить `src/RpcRegistry.cpp src/CoreMethods.cpp src/Events.cpp src/PawnApi.cpp` в
    `add_library`; таргет `registry_test` (`tests/registry_test.cpp` + `src/RpcRegistry.cpp`).
12. **Verify** — сборка + unit-тест + amxxpc (инклюд/пример 0/0).
13. **Record** — журнал `0004`; `design/12` → утверждён (`docs/design/README.md`); OQ-2/3/10/13/14 →
    `docs/08` §1; синхронизировать `docs/03` (§5/§5а/§6/§7) и OQ-таблицу `.omo/plans/0001`.

## Acceptance criteria (verifiable)

- Сборка win/linux — 0 ошибок.
- Off-line `tests/registry_test` — зелёный (коллизии, core-override запрет, метаданные `rpc.methods`).
- `amxxpc`: `include/AmxxRpc/Core.inc` через пример — 0/0.
- На тест-сервере (владелец): плагин регистрирует `demo.echo` → вызов по RPC → ответ; async-ответ;
  таймаут `-32005`; чужой `Reply` отклонён (ownership); подписка → push `player_connect`;
  `server.exec`/`cvar.set`/`players.list` работают.
- Снятие методов на `PLUGINSUNLOADING`; pending завершаются ошибкой.

## Verification (точные команды)

```
# Модуль (Linux 32-bit; cmake локально нет)
gcc -m32 -fPIC -c third_party/parson/parson.c -o /tmp/parson.o
g++ -m32 -std=c++17 -fPIC -shared -DHAVE_STDINT_H -DNOMINMAX \
  -Isrc -Ithird_party/amxx -Ithird_party/amxx/sdk -Ithird_party/metamod \
  -Ithird_party/hlsdk/common -Ithird_party/hlsdk/dlls -Ithird_party/hlsdk/engine \
  -Ithird_party/hlsdk/game_shared -Ithird_party/hlsdk/public -Ithird_party/parson \
  -ffunction-sections -fdata-sections -fcf-protection=none -Wall -Wextra \
  -Wl,--gc-sections -static-libstdc++ -static-libgcc \
  -o /tmp/amxxrpc_amxx_i386.so src/*.cpp third_party/amxx/sdk/amxxmodule.cpp /tmp/parson.o \
  -ldl -lm -lpthread

# Off-line unit-тесты
g++ -m32 -std=c++17 -Isrc -Ithird_party/parson tests/protocol_test.cpp src/Protocol.cpp /tmp/parson.o -o /tmp/protocol_test && /tmp/protocol_test
g++ -m32 -std=c++17 -Isrc -Ithird_party/parson tests/registry_test.cpp src/RpcRegistry.cpp /tmp/parson.o -o /tmp/registry_test && /tmp/registry_test

# CMake (canonical acceptance; локально cmake нет — deferred to CI/owner):
#   cmake -S . -B build-lin -DCMAKE_CXX_FLAGS=-m32 && cmake --build build-lin   # + target registry_test

# Pawn (из нативного ФС)
amxxpc examples/AmxxRpcExample.sma -o/tmp/AmxxRpcExample.amxx -iinclude -i<amxx-include-dir>   # 0/0
```

> Рантайм-smoke/Windows/CMake — deferred to owner/CI.

## MUST NOT

- Обращаться к AMXX/движку из I/O-потока; логировать токен.
- Реализовывать фейк-игроков/ботов/MCP (Этапы 3–4).
- Позволять плагину переопределять core-методы или отвечать за чужой requestId.
- Вызывать SP-forward без проверки живости владельца (UAF).

## Out of scope

- Захват вывода `server.exec`, `logs.tail`, раундовые/килл-события (отложены, `design/12` §12).
- Фейки (Этап 3), боты (Этап 4), упаковка (Этап 5).

## Risks / notes

- **SP-forward UAF** при индивидуальной выгрузке плагина (нет per-plugin хука) — митигация `design/12` §8;
  оговорка в FR-RPC-009.
- Локально нет HLDS → рантайм регистрации/события проверяются владельцем; off-line — только реестр.
