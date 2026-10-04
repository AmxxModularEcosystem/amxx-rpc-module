# 0003. Этап 1 — Транспорт (TCP + JSON-RPC)

**Дата:** 2026-10-04 · **Этап:** 1 Транспорт · **Статус:** реализовано; сборка + off-line unit-тест
зелёные; рантайм-smoke/Windows/CMake — отложены (нет HLDS/cmake локально)

## Контекст

Второй этап дорожной карты. Дизайн — `docs/design/11` (§1–14, включая резолвы ревью), `design/16`;
план — `.omo/plans/0003`. Дизайн-ревью `oracle` (блокеры B1–B4 + M1–M12 свёрнуты), ревью плана
`momus` (REJECT → правки → OKAY). Реализация поручена субагенту (`developer`), проверена независимо.

## Сделано

- **Вендоринг `parson` 1.2.1** (MIT) в `third_party/parson/` (+ LICENSE, `VERSIONS.md`); компилируется как C.
- **Подсистемы:** `src/Queue.h` (bounded mutex+condvar), `src/Protocol.{h,cpp}` (JSON-RPC 2.0,
  без зависимости от SDK), `src/Log.{h,cpp}` (файл + ограниченная очередь в main), `src/Config.{h,cpp}`,
  `src/Transport.{h,cpp}` (один неблокирующий I/O-поток, `select` + wakeup, per-session буферы,
  const-time auth, `SIGPIPE`/`MSG_NOSIGNAL`, монотонный `sessionId`), `src/RpcDispatch.{h,cpp}`
  (`StartFrame`-дренаж, per-session outbox с `isNotification`, `rpc.ping/version/methods`).
- **`src/main.cpp`** — init Config→Log→Queue→Transport; `FN_StartFrame_Post`; idempotent shutdown
  на `OnAmxxDetach`+`OnMetaDetach`; команды `amxxrpc_reload/status/clients/help`.
- **`src/moduleconfig.h`** — `FN_StartFrame_Post`, `FN_META_DETACH`.
- `configs/amxxrpc.cfg`, `tests/protocol_test.cpp`, `scripts/smoke_tcp.py`, `CMakeLists.txt`
  (`C CXX` + parson + Тест-таргет).

## Решения и обоснование

- Все резолвы ревью — `design/11` §13–14 (SIGPIPE, per-session bounded outbox, wakeup для outbox,
  монотонный `sessionId`, raw `id`, reload pre-bind, depth-guard до parson, лог в файл).
- Закрыты OQ-1, OQ-4, OQ-6, OQ-8, OQ-11, OQ-12, OQ-15, OQ-17 (перенесены в `docs/08` §1, AD-11…AD-18).
- **B-4 (pump при пустом сервере)** — абстракция `Pump`: реализация не привязана жёстко; проверка
  тика `StartFrame` при пустом сервере остаётся за владельцем на тест-сервере.

## Верификация (доказательства, независимо перепроверено)

- **Linux-модуль:** точной командой → exit 0 (benign `DT_TEXTREL` от `parson.o` без `-fPIC`;
  CMake добавляет `-fPIC`). Экспорты 6/6: `AMXX_Query/Attach/Detach`, `Meta_Query/Attach`, `GiveFnptrsToDll`.
- **Off-line unit-тест:** `tests/protocol_test` → **54 checks, 0 failures**.
- **amxxpc 1.10.5428:** `examples/AmxxRpcExample.sma` → 0/0 (`Done.`).
- I/O-путь (`Protocol.cpp`/`Transport.cpp`/`Queue.h`/`Log.cpp`) не содержит AMXX-вызовов.

## Открытые нити

- **Рантайм TCP-smoke не выполнен** (нет HLDS) — `scripts/smoke_tcp.py` на тест-сервере (владелец).
- **Windows/CMake-сборка** не проверена (нет MSVC/cmake) — CI/владелец.
- **B-4** (тикает ли `StartFrame` при пустом сервере) — на тест-сервере.
- Token-only reload не ре-бинджнит; host/port — pre-bind restart (SO_REUSEADDR).
- `deviation`: субагент добавил `FN_META_DETACH` (нужен для shutdown на `Meta_Detach`).

## Затронутые артефакты

- `src/*`, `configs/amxxrpc.cfg`, `tests/protocol_test.cpp`, `scripts/smoke_tcp.py`, `CMakeLists.txt`,
  `third_party/{parson,VERSIONS.md}`, `.gitattributes`, `.gitignore`;
- `docs/design/README.md`, `docs/08`, `.omo/plans/0001`, `.omo/plans/0003`.

## Дальше

- **Этап 2 — Реестр методов и Pawn-API** (`design/12`): диспетчер, встроенное ядро
  (`server.exec`/`cvar.*`/`players.*`/`events.*`), нативы `ARpc_Core_*`, обработка OQ-2/3/13/14.
