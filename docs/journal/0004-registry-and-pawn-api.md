# 0004. Этап 2 — Реестр методов и Pawn-API

**Дата:** 2026-10-04 · **Этап:** 2 Реестр/Pawn-API · **Статус:** реализовано; сборка + off-line
тесты + amxxpc зелёные; рантайм/Windows/CMake — отложены

## Контекст

Третий этап дорожной карты, поверх транспорта Этапа 1. Дизайн — `docs/design/12` (§1–12, включая
резолвы ревью), план — `.omo/plans/0004`. Ревью дизайна `oracle` (B1–B3 + M1–M8 свёрнуты),
ревью плана `momus` (REJECT → правки → OKAY). Реализация — субагентом, проверена независимо.

## Сделано

- **Реестр** `src/RpcRegistry.{h,cpp}`: `name→{source,description,pluginId,forwardId}`, коллизии
  (Pawn dup → ошибка; запрет override core/transport/`rpc.*`), каталог `[{name,source,description}]`;
  отделён от AMXX (`IRpcForwardRegistrar`).
- **Pawn-нативы** `src/PawnApi.{h,cpp}`: регистрация через `MF_AddNatives`/`AMX_NATIVE_INFO`;
  нативы `ARpc_Core_*`; ownership-check (`MF_FindScriptByAmx`); SP-forward
  (`MF_RegisterSPForwardByName(MF_GetScriptAmx(pid), …)`, `MF_ExecuteForward`, `MF_UnregisterSPForward`);
  валидация JSON (parson).
- **Ядро** `src/CoreMethods.{h,cpp}`: `server.exec` (`{ok:true}`), `cvar.get/set`, `players.list/get`,
  `events.subscribe/unsubscribe`, `rpc.methods`; `rpc.ping/version` сохранены.
- **События** `src/Events.{h,cpp}`: карта подписок (main), `Emit`, ленивая прунинг
  (`Transport_IsSessionAlive`); хуки `FN_ClientConnect/Disconnect/ServerActivate` → `player_connect`/
  `player_disconnect`/`map_start`.
- **Рефактор Этапа 1:** `Outstanding{methodName,pluginId}`; `Rpc_Reply/Rpc_ReplyError(handle,…)`
  (поиск `sessionId`); диспатч через реестр; `Rpc_PushNotification`, `Rpc_FailPawnRequests`.
- **`Transport_AuthenticatedCount()`** + `Transport_IsSessionAlive()`.
- **`moduleconfig.h`** + `FN_AMXX_PLUGINSUNLOADING` (снятие всех Pawn-методов + fail pending).
- **Core.inc** (нативы + `ARP_ERR_*` + `ARP_VERSION`), **пример** `demo.echo`, **`tests/registry_test.cpp`**,
  **CMakeLists** (+4 источника + таргет `registry_test`).

## Решения и обоснование

- Резолвы `design/12` §12: SP-forward через `MF_GetScriptAmx`; не полагаться на код возврата
  `MF_ExecuteForward`; ownership-check; монотонный `requestId`; `server.exec` без захвата вывода
  (FR-RPC-008); `logs.tail`/round-kill отложены (FR-RPC-005); индивидуальная выгрузка плагина —
  оговорка (FR-RPC-009).
- Закрыты OQ-2/3/10/13/14 (`docs/08` §1, AD-19…AD-23).

## Верификация (независимо перепроверено)

- Модуль: exit 0; warnings только из вендоренного `amxxmodule.cpp` (не `src/`), `-Wall -Wextra` чисто.
- Экспорты 6/6; `protocol_test` **54/0**; `registry_test` **60/0**; `amxxpc` пример — 0/0.
- Devation: Pawn-строки с `^"` (backslash-escape валит Linux-`amxxpc` `bad_alloc`); `MODULE_VERSION`
  поднят до `0.1.0` (согласован с `ARP_VERSION`).

## Открытые нити

- **Рантайм не проверен** (нет HLDS): регистрация метода/async/таймаут/ownership/push — владелец.
- Windows/CMake-сборка не проверена.
- Индивидуальная выгрузка плагина надёжно не поддерживается (FR-RPC-009 оговорка).

## Затронутые артефакты

- `src/{RpcRegistry,CoreMethods,Events,PawnApi,RpcDispatch,Transport,main}.{h,cpp}`, `src/moduleconfig.h`,
  `include/AmxxRpc/Core.inc`, `examples/AmxxRpcExample.sma`, `tests/registry_test.cpp`, `CMakeLists.txt`;
- `docs/01` (FR-RPC-005/008/009), `docs/03` (ревизия), `docs/08` (AD-19…23), `docs/design/README.md`,
  `.omo/plans/0001`.

## Дальше

- **Этап 3 — Фейк-игроки** (`design/13`): движковый слой, подмена authid, usercmd-think, `fake.*`.
