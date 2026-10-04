# Work Plan: Этап 3 — Фейк-игроки (AmxxRpc)

## Profile bindings

- Ревьюер дизайна: `oracle`. Ревьюер плана: `momus`.
- Дизайн-гейт: [`docs/design/13-fake-players.md`](../../docs/design/13-fake-players.md).
- Планы: `.omo/plans/`. Журнал: `docs/journal/` (запись `0005`).
- **Гейт реализации:** после ревью дизайна/плана; `design/13` → утверждён.
- Коммит — владелец разрешил коммитить каждый этап.
- Закрывает OQ: OQ-5.

## Goal

Движковый слой фейк-игроков: создание/удаление, подмена authid, покадровый usercmd-драйв,
RPC-методы `fake.*` (болванчики для сценариев). YAPB — Этап 4.

## Deliverables

| Артефакт | Что |
|---|---|
| `src/Fake.{h,cpp}` | `FakeRecord`, реестр, create/remove, think (RunPlayerMove), `FN_GetPlayerAuthId`-хук |
| `src/FakeMethods.{h,cpp}` | RPC `fake.*` (create/remove/list/get/move/look/stop/buttons/set/authid) |
| `src/moduleconfig.h` | + `FN_GetPlayerAuthId` |
| `src/main.cpp` | + `Fake_Init/Shutdown`, `Fake_Think` в `StartFrame`, хуки смерти/карты |
| `configs/amxxrpc.cfg` | + `fake_max` |
| `src/Config.{h,cpp}` | + `fake_max` (валидация) |
| `CMakeLists.txt` | + `src/Fake.cpp src/FakeMethods.cpp` |
| `docs/journal/0005-fake-players.md` | запись |
| `docs/04`, `docs/03`, `docs/design/README.md` | синк §2 (порядок) под `design/13` §3; статус `design/13` → утверждён |

## Steps (ordered)

1. **FakeRecord + реестр** (main), `fake_max`, защита реальных игроков.
2. **create** (ревизия `design/13` §3): `pfnCreateFakeClient` → **сразу зарегистрировать `FakeRecord`+authid**
   → `FL_FAKECLIENT|FL_CLIENT` → фабрика `player()` (`MUTIL_CallGameEntity(PLID,"player",…)`, если доступна)
   → userinfo → `MDLL_ClientConnect`/`ClientPutInServer` → **`MDLL_ClientUserInfoChanged`** (регистрация в AMXX).
3. **authid-хук** `FN_GetPlayerAuthId` (pre, `MRES_SUPERCEDE`), постоянный буфер, снятие при remove.
4. **think** — в `StartFrame`: `pfnRunPlayerMove(ent, angles, fwd, side, up, buttons, impulse, msec=frametime)`
   для alive; guard dead/not-spawned.
5. **remove** — `MDLL_ClientDisconnect` + удаление эдикта + освобождение записи/буфера.
6. **FakeMethods** — регистрация `fake.*` в реестре (source=builtin) **+ проводка диспетча**:
   `Core_Dispatch` делегирует неизвестные `fake.*` в `Fake_Dispatch` (иначе методы дадут `-32601`);
   `FakeMethods_Init` из `Fake_Init`/`Core_Init`; валидация params; отказ на реальном игроке.
7. **main.cpp** — init/shutdown, think, обработка смерти/`map_start` (пересчёт `alive`/сброс).
8. **config** — `fake_max`.
9. **CMake** — новые источники.
10. **Verify** — сборка + оба unit-теста (регрессия) + amxxpc 0/0.
11. **Record** — журнал `0005`; `design/13` → утверждён; OQ-5 → `docs/08` §1.

## Acceptance criteria (verifiable)

- Сборка win/linux 0 ошибок; off-line тесты `protocol_test`/`registry_test` — регрессия зелёная.
- `amxxpc`: пример — 0/0.
- На тест-сервере (владелец): `fake.create` создаёт слот с `FL_FAKECLIENT` и **уникальным authid**
  (проверка `get_user_authid`); **фейк виден в AMXX** (`get_players`), т.к. регистрация идёт через
  `ClientUserInfoChanged`; `fake.move/look/buttons` двигают/стреляют; `fake.set`; `fake.remove`
  освобождает слот; `fake.*` отвергает реального игрока; лимит `fake_max`.

## Verification (точные команды)

```
gcc -m32 -fPIC -c third_party/parson/parson.c -o /tmp/parson.o
g++ -m32 -std=c++17 -fPIC -shared -DHAVE_STDINT_H -DNOMINMAX \
  -Isrc -Ithird_party/amxx -Ithird_party/amxx/sdk -Ithird_party/metamod \
  -Ithird_party/hlsdk/common -Ithird_party/hlsdk/dlls -Ithird_party/hlsdk/engine \
  -Ithird_party/hlsdk/game_shared -Ithird_party/hlsdk/public -Ithird_party/parson \
  -ffunction-sections -fdata-sections -fcf-protection=none -Wall -Wextra \
  -Wl,--gc-sections -static-libstdc++ -static-libgcc \
  -o /tmp/amxxrpc_amxx_i386.so src/*.cpp third_party/amxx/sdk/amxxmodule.cpp /tmp/parson.o \
  -ldl -lm -lpthread
g++ -m32 -std=c++17 -Isrc -Ithird_party/parson tests/protocol_test.cpp src/Protocol.cpp /tmp/parson.o -o /tmp/protocol_test && /tmp/protocol_test
g++ -m32 -std=c++17 -Isrc -Ithird_party/parson tests/registry_test.cpp src/RpcRegistry.cpp /tmp/parson.o -o /tmp/registry_test && /tmp/registry_test
# amxxpc (из нативного ФС) — пример 0/0
```

> Рантайм проверяется **на реальном сервере** (e2e-пайплайн готов): сборка Windows-DLL через
> `zig c++ -target x86-windows-gnu ... cmake/amxxrpc.def` (undecorated `GiveFnptrsToDll`),
> деплой в `hlds-server`, перезапуск, smoke/сценарный тест из Windows-Python. Engine-уровень
> (`CreateFakeClient`/authid `ClientUserInfoChanged`/`RunPlayerMove`) проверяется там же.

## MUST NOT

- Обращаться к AMXX/движку из I/O-потока.
- Управлять реальными игроками через `fake.*`; превышать `fake_max`.
- Вызывать `RunPlayerMove` для dead/not-spawned (краш/призрак).
- Реализовывать YAPB/ботов (Этап 4) / MCP.

## Out of scope

- YAPB-адаптер (Этап 4), упаковка (Этап 5).

## Risks / notes

- Движковые вызовы не тестируются локально (нет HLDS) — рантайм на владельце.
- Порядок authid до `ClientConnect`; удаление/освобождение слота; покадровый драйв.
- `StartFrame` при пустом сервере (B-4) — предпосылка для управления фейками; подтверждается владельцем.
