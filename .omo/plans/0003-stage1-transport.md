# Work Plan: Этап 1 — Транспорт (AmxxRpc)

## Profile bindings

- Ревьюер дизайна: `oracle`. Ревьюер плана: `momus`.
- Дизайн-гейт: [`docs/design/11-transport-and-protocol.md`](../../docs/design/11-transport-and-protocol.md),
  [`docs/design/16-config-and-observability.md`](../../docs/design/16-config-and-observability.md).
- Планы: `.omo/plans/`. Журнал: `docs/journal/` (запись `0003`). OQ: `docs/08` §3.
- **Гейт реализации:** не начинать, пока дизайн (`oracle`) и план (`momus`) не пройдены; `design/11`/`design/16`
  перевести в утверждённые (`docs/design/README.md`).
- Верификация: см. §Verification. Коммит — по указанию владельца (владелец: коммитить каждый этап).
- Закрывает OQ: OQ-1, OQ-4, OQ-6, OQ-8, OQ-11, OQ-12, OQ-15, OQ-17.

## Goal

Поднять транспорт: TCP loopback + JSON-RPC 2.0 (фрейминг, auth), I/O-поток + мьютекс-очереди +
дренаж в `StartFrame`; конфиг и серверные команды. Методы: `rpc.auth` (транспортный),
`rpc.ping`, `rpc.version`, `rpc.methods`.

## Deliverables

| Артефакт | Что |
|---|---|
| `third_party/parson/` | JSON-библиотека (vendored, MIT) + запись в `VERSIONS.md` |
| `src/Config.{h,cpp}` | чтение конфига, валидация, fail-fast, reload |
| `src/Log.{h,cpp}` | потокобезопасный лог (файл + ограниченная очередь в main; без AMXX из I/O) |
| `src/Queue.h` | мьютекс-очередь + condition variable |
| `src/Protocol.{h,cpp}` | JSON-RPC кодек (без зависимости от SDK), коды ошибок, depth/размер guard |
| `src/Transport.{h,cpp}` | listen/accept, per-session in/out буферы (bounded), `select`, фрейминг, auth, I/O-поток, wakeup |
| `src/RpcDispatch.{h,cpp}` | дренаж `I/O→main` в `StartFrame`; `rpc.ping`/`rpc.version`/`rpc.methods` |
| `src/main.cpp` | подключение подсистем, `FN_StartFrame_Post`, idempotent shutdown |
| `src/moduleconfig.h` | + `#define FN_StartFrame_Post StartFrame_Post` |
| `configs/amxxrpc.cfg` | пример конфига |
| `tests/protocol_test.cpp` | **off-line** unit-тест парсера/кодека (без AMXX) |
| `scripts/smoke_tcp.py` | TCP smoke-клиент (auth/ping/version/methods/ошибки) |
| `CMakeLists.txt` | + `C CXX`, parson, потоки |
| `docs/design/README.md`, `docs/08` §1, `.omo/plans/0001` OQ-таблица | синхронизация статусов/OQ |
| `docs/journal/0003-transport.md` | запись этапа |

## Steps (ordered)

1. **B-4 (prerequisite, owner/test-server)** — подтвердить, что `StartFrame` тикает при пустом/спящем
   сервере (счётчик тиков в лог / `amxxrpc_status`). Если не тикает — зафиксировать альтернативный pump
   **до** реализации Transport. Локально HLDS нет → выполняется владельцем; до ответа Transport
   пишется так, чтобы pump был сменяемым (абстракция `Pump`).
2. **Vendor parson** в `third_party/parson/` (+ `VERSIONS.md`, лицензия, `NOTICE`).
3. **Config** — парсер `ключ=значение`, дефолты/валидация (token≥16, port, `max_connections ≤ FD_SETSIZE-1`),
   fail-fast; ключи `pre_auth_timeout`, `max_session_out_bytes`; live/restart-области; reload с pre-bind.
4. **Log** — уровни; из I/O — файл + ограниченная очередь в main (не `MF_Log` из I/O).
5. **Queue** — `Queue<T>` (mutex+condvar), `TryPush`/`Pop`, bounded.
6. **Protocol** — парсинг/сборка JSON-RPC; сырой `id` (эхо дословно); ошибки `-327xx`/`-320xx`;
   depth/размер guard **до** parson; батчи — отвергать `-32600`.
7. **Transport** — non-blocking listen/accept (`SO_REUSEADDR`, Windows `SO_EXCLUSIVEADDRUSE`),
   per-session in/out буферы, `select` (+wakeup-пайп), фрейминг до `\n` (срез `\r`), `rpc.auth`
   (const-time), `idle`/`pre_auth` таймаут, `SIG_IGN`/`MSG_NOSIGNAL`, `EINTR`/`EMFILE`, I/O-поток.
8. **RpcDispatch** — `StartFrame`-дренаж с бюджетом; handle/id-таблица (сырой `id`), таймауты `-32005`;
   `rpc.ping`/`rpc.version`/`rpc.methods` (спец-кейс `rpc.auth` — «transport»); per-session outbox.
9. **main.cpp** — init order (Config→Log→Queue→Transport), `FN_StartFrame_Post`, idempotent shutdown
   (wakeup+join; запрет accept → I/O закрывает сокеты → join).
10. **Commands** — `amxxrpc_reload/status/clients/help`; доступ: reload/status/clients → `ADMIN_RCON`,
    help — без ограничений (`design/16` §4).
11. **CMake** — `project(amxxrpc C CXX)`; parson.c как C; threads.
12. **Tests/scripts** — `tests/protocol_test.cpp` (off-line) + `scripts/smoke_tcp.py`.
13. **Verify** — сборка (g++ эквивалент + отдельная сборка unit-теста) + amxxpc-регрессия.
14. **Record** — журнал `0003` + индекс; перенести OQ-4/OQ-8 (и OQ-1/6/11/12/15/17) в `docs/08` §1;
    обновить OQ-таблицу `.omo/plans/0001`; перевести `design/11`/`design/16` в утверждённые.

## Резолвы ревью дизайна (свёрнуты в `design/11` §13–14, `design/16` §3–5)

- **B-1** `SIGPIPE`; **B-2/B-3** per-session bounded outbox + cap выходного буфера; **B-4** см. шаг 1.
- **M-1** wakeup для «outbox непуст»; **M-2/M-3** монотонный `sessionId`, main дропает мёртвой сессии;
  **M-4** `RpcOut.isNotification`; **M-5** reload pre-bind; **M-6** depth-guard до parson;
  **M-7** OQ-4 здесь; **M-8** `rpc.methods`; **M-9** сырой `id`; **M-10** live-reload; **M-11** доступ команд;
  **M-12** лог в файл + ограниченная очередь.

## Acceptance criteria (verifiable)

- Сборка win/linux — 0 ошибок; модуль грузится, лог «listening on host:port».
- Off-line `tests/protocol_test` — все кейсы зелёные (parse/serialize/ошибки/depth/сырой `id`).
- Smoke на тест-сервере: `rpc.auth` верный → `ok`; неверный → `-32001` + close;
  `rpc.ping`/`rpc.version`/`rpc.methods`; невалидный JSON → `-32700` + close; батч → `-32600`.
- Переполнение `inbox` → `-32004`; таймаут → `-32005`; запись в закрытый сокет не убивает сервер (SIGPIPE).
- Shutdown без зависания `join`; `amxxrpc_status` показывает состояние; reload с pre-bind (при занятом
  порте старый транспорт продолжает работать).
- **B-4** подтверждён (или зафиксирован альтернативный pump).
- `amxxpc`: пример Этапа 0 — 0/0 (регрессия).

## Verification (точные команды)

```
# Модуль, Linux 32-bit (эквивалент CMake Linux-ветки; cmake в окружении отсутствует)
gcc -m32 -c third_party/parson/parson.c -o /tmp/parson.o
g++ -m32 -std=c++17 -fPIC -shared -DHAVE_STDINT_H -DNOMINMAX \
  -Isrc -Ithird_party/amxx -Ithird_party/amxx/sdk -Ithird_party/metamod \
  -Ithird_party/hlsdk/common -Ithird_party/hlsdk/dlls -Ithird_party/hlsdk/engine \
  -Ithird_party/hlsdk/game_shared -Ithird_party/hlsdk/public -Ithird_party/parson \
  -ffunction-sections -fdata-sections -fcf-protection=none \
  -Wl,--gc-sections -static-libstdc++ -static-libgcc \
  -o /tmp/amxxrpc_amxx_i386.so src/*.cpp third_party/amxx/sdk/amxxmodule.cpp /tmp/parson.o \
  -ldl -lm -lpthread

# Off-line unit-тест протокола (без AMXX)
g++ -m32 -std=c++17 -Isrc -Ithird_party/parson tests/protocol_test.cpp src/Protocol.cpp /tmp/parson.o -o /tmp/protocol_test && /tmp/protocol_test

# Регрессия Pawn (из нативного ФС; linux-amxxpc не читает /mnt/*)
amxxpc examples/AmxxRpcExample.sma -o/tmp/AmxxRpcExample.amxx -iinclude -i<amxx-include-dir>   # 0/0

# Smoke на тест-сервере (владелец; локально HLDS нет) — deferred
python3 scripts/smoke_tcp.py 127.0.0.1 27016 <token>
```

> Windows/CMake-сборка и socket-smoke локально недоступны → **deferred to CI/owner**.

## MUST NOT

- Обращаться к AMXX/движку из I/O-потока; логировать токен.
- Реализовывать реестр/Pawn-нативы/фейки/ботов (Этапы 2–4).
- Вводить MCP. Блокировать главный поток на I/O.
- Дропать **ответы** при переполнении (только нотификации; переполнение сессии → close).

## Out of scope

- `server.exec`/`cvar.*`/`players.*`/`events.*` (Этап 2), фейки (Этап 3), боты (Этап 4), упаковка (Этап 5).
- JSON-RPC батчи (OQ-6 закрыт: не поддерживаем).

## Risks / notes

- B-4 (pump при пустом сервере) — решается до Transport-в-игре; абстракция `Pump` позволяет замену.
- `select` wakeup и частичная запись — ключевые места (`design/11` §3/§6).
- `parson.c` компилируется как **C** (`gcc`/CMake C); при C++-компиляции возможны warnings — не полагаться.
- Без HLDS нет рантайм-smoke: протокол — off-line unit-тестом, транспорт — сборкой; smoke — владелец.
