# Work Plan: Этап 4 — Опциональный YAPB-адаптер (AmxxRpc)

## Profile bindings

- Ревьюер дизайна: `oracle`. Ревьюер плана: `momus`.
- Дизайн-гейт: [`docs/design/14-bot-adapter-yapb.md`](../../docs/design/14-bot-adapter-yapb.md).
- Планы: `.omo/plans/`. Журнал: `docs/journal/` (запись `0006`).
- **Гейт реализации:** после ревью дизайна/плана; `design/14` → утверждён.
- Коммит — владелец разрешил коммитить каждый этап.
- Закрывает OQ: OQ-7.

## Goal

Опциональный адаптер YAPB (`IBotModule`) для навигации: динамическая загрузка/детект, RPC `bot.*`,
**graceful degradation** без YAPB (никогда не обязателен). Атака — своим usercmd-слоем (Этап 3).

## Deliverables

| Артефакт | Что |
|---|---|
| `third_party/yapb/module.h` | **Вендоренный** YAPB `IBotModule` (зафиксированный коммит) + `VERSIONS.md` |
| `src/Bot.{h,cpp}` | детект/загрузка YAPB, resolve `GetBotAPI`, обёртки `IBotModule` (через указатель), `Bot_Available`, `Bot_Init/Shutdown` (+ ownership) |
| `src/BotMethods.{h,cpp}` | RPC `bot.available/add/list/goal/look/freeze/status`; регистрация+`Bot_Dispatch`; деградация `-32002`; `IsBot`-гард |
| `src/main.cpp` | `Bot_Init` в attach, `Bot_Shutdown` в shutdown, `Bot_OnMapStart` (сброс указателя) в ServerActivate; `CmdStatus` — YAPB |
| `src/CoreMethods.cpp` | делегирование `bot.*` → `Bot_Dispatch` |
| `CMakeLists.txt` | + `src/Bot.cpp src/BotMethods.cpp` |
| `tests/bot_test.cpp` | off-line с mock `IBotModule` (деградация/валидация/IsBot) |
| `scripts/e2e_bot.py` | e2e деградации на сервере без YAPB |
| `docs/journal/0006-bot-adapter-yapb.md` | запись |
| `docs/design/README.md`, `docs/08` | `design/14` → утверждён; OQ-7 → §1, снять из §3 |

## Steps (ordered)

1. **Vendor YAPB `module.h`** (`IBotModule`) на зафиксированном коммите; `VERSIONS.md`.
2. **Bot** — детект: Win `GetModuleHandleA("yapb.dll")`, Linux `dlopen` по пути/`dl_iterate_phdr`;
   приоритет — `yapb_amxx` уже загрузил ядро; `GetBotAPI(kBotModuleVersion)` (не считать `!=nullptr`
   признаком готовности — нужен детект ядра); **не self-load** по умолчанию; ownership-флаг.
3. **Обёртки `IBotModule`** — `IsBotsInGame/IsBot/GetBotCount/AddBot/SetBotGoalOrigin/SetBotLookAt/
   SetBotMovement/GetBotOrigin/GetBotEnemy/GetBotWeapon/GetBotTask`.
4. **BotMethods** — `bot.available` (`{available,version?}`), `bot.add → {queued:true}`,
   `bot.list`, `bot.goal`/`bot.look`/`bot.freeze`/`bot.status`; `IsBot`-гард (`-32602` для не-ботов);
   YAPB нет → `-32002` (кроме `available`).
5. **Wire** — регистрация (source=builtin) + `Core_Dispatch` делегирует `bot.*` → `Bot_Dispatch`;
   `BotMethods_Init` из `Bot_Init`.
6. **main.cpp** — `Bot_Init`/`Bot_Shutdown` (ownership)/`Bot_OnMapStart`; `CmdStatus` + YAPB.
7. **CMake** — новые источники + таргет `bot_test`.
8. **Mock-seam + tests/bot_test.cpp** — деградация/валидация/IsBot (off-line).
9. **scripts/e2e_bot.py** — auth → `available:false`; `bot.*` → `-32002`; `rpc.methods` содержит `bot.*`.
10. **Verify** — сборка Linux/Windows; off-line тесты (`protocol`/`registry`/`bot`); e2e деградации.
11. **Record** — журнал `0006`; `design/14` → утверждён (README); OQ-7 → `docs/08` §1 (снять из §3).

## Acceptance criteria (verifiable)

- Сборка win/linux 0 ошибок; off-line тесты зелёные (регрессия).
- E2E на сервере без YAPB: `bot.available → {available:false}`; `bot.add/goal/look/freeze/status → -32002`;
  `rpc.methods` включает `bot.*`; smoke (12/0) и fake e2e — по-прежнему зелёные.
- Модуль не падает и не требует YAPB.

## Verification (точные команды)

```
# Linux
gcc -m32 -fPIC -c third_party/parson/parson.c -o /tmp/parson.o
g++ -m32 -std=c++17 -fPIC -shared -DHAVE_STDINT_H -DNOMINMAX -Isrc -Ithird_party/amxx -Ithird_party/amxx/sdk \
  -Ithird_party/metamod -Ithird_party/hlsdk/common -Ithird_party/hlsdk/dlls -Ithird_party/hlsdk/engine \
  -Ithird_party/hlsdk/game_shared -Ithird_party/hlsdk/public -Ithird_party/parson -ffunction-sections -fdata-sections \
  -fcf-protection=none -Wl,--gc-sections -static-libstdc++ -static-libgcc -o /tmp/amxxrpc_amxx_i386.so \
  src/*.cpp third_party/amxx/sdk/amxxmodule.cpp /tmp/parson.o -ldl -lm -lpthread
# Windows (zig + .def)
ZIG=/tmp/zig-linux-x86_64-0.13.0/zig
$ZIG cc -target x86-windows-gnu -c third_party/parson/parson.c -o /tmp/parson_win.o
$ZIG c++ -target x86-windows-gnu -fno-sanitize=all -shared -std=c++17 -DHAVE_STDINT_H -DNOMINMAX -D_declspec=__declspec \
  -Isrc -Ithird_party/amxx -Ithird_party/amxx/sdk -Ithird_party/metamod -Ithird_party/hlsdk/common -Ithird_party/hlsdk/dlls \
  -Ithird_party/hlsdk/engine -Ithird_party/hlsdk/game_shared -Ithird_party/hlsdk/public -Ithird_party/parson \
  -o /tmp/amxxrpc_amxx.dll src/*.cpp third_party/amxx/sdk/amxxmodule.cpp /tmp/parson_win.o cmake/amxxrpc.def -lws2_32
# Off-line тесты (регрессия + bot)
g++ -m32 -std=c++17 -Isrc -Ithird_party/parson tests/protocol_test.cpp src/Protocol.cpp /tmp/parson.o -o /tmp/protocol_test && /tmp/protocol_test
g++ -m32 -std=c++17 -Isrc -Ithird_party/parson tests/registry_test.cpp src/RpcRegistry.cpp /tmp/parson.o -o /tmp/registry_test && /tmp/registry_test
g++ -m32 -std=c++17 -Isrc tests/bot_test.cpp src/Bot.cpp -o /tmp/bot_test && /tmp/bot_test

# e2e degradation (Windows python, сервер без YAPB)
python scripts/e2e_bot.py 127.0.0.1 27016 <token>
```
> YAPB-present путь — вне окружения (нет YAPB) → нить владельцу.

## MUST NOT

- Обязательная зависимость от YAPB; падение без него.
- Атака через YAPB (`IBotModule` не имеет attack) — только навигация/прицел.
- AMXX/движок из I/O-потока.
- Реализовывать MCP.

## Out of scope

- Упаковка/поставка (Этап 5). Интеграция YAPB с on-server установкой — владелец.

## Risks / notes

- ABI `IBotModule` (C++/YSTL) — R-3; MSVC-совместимость при наличии YAPB.
- Загрузка YAPB как плагина метамода vs как библиотеки — поведение `GetBotAPI` уточняется.
- E2E проверяет только деградацию; presence-путь — нить.
