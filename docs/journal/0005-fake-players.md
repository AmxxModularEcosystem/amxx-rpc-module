# 0005. Этап 3 — Фейк-игроки (движковый слой)

**Дата:** 2026-10-04 · **Этап:** 3 Фейк-игроки · **Статус:** реализовано; сборка Linux/Windows +
off-line тесты зелёные; **e2e на реальном HLDS-сервере — lifecycle фейка подтверждён** (authid-override
реализован; независимое наблюдение плагином — нить)

## Контекст

Четвёртый этап дорожной карты, поверх транспорта (Этап 1) и реестра/Pawn-API (Этап 2). Дизайн —
`docs/design/13` (§1–11, включая резолвы ревью §10a), план — `.omo/plans/0005`. Требования —
`docs/04`, `docs/01` (AR-014…016, FR-FAKE-*). Закрывает OQ-5.

Критичный порядок (из исходников ReHLDS/AMXX): `pfnCreateFakeClient` **сам** ставит
`FL_FAKECLIENT|FL_CLIENT` и вызывает `ClientUserInfoChanged` → AMXX `C_ClientUserInfoChanged_Post`
регистрирует бота и читает `GETPLAYERAUTHID`. Значит authid должен быть доступен **до** вызова —
через модульный `g_pendingAuthid` и pre-хук `FN_GetPlayerAuthId`.

## Сделано

- **`src/Fake.{h,cpp}`**: `FakeRecord` (entIndex/ent/authid/name/alive/viewAngles/fwd/side/up/
  buttons/impulse/pendingRemove); реестр `entIndex → FakeRecord` (main, `std::map` — стабильные
  буферы authid); `Fake_Init/SetMax/Shutdown`; `Fake_Create` (лимит `fake_max`; `g_pendingAuthid`
  → `pfnCreateFakeClient` → регистрация записи → `MDLL_ClientConnect`/`ClientPutInServer` →
  `FL_FAKECLIENT|FL_CLIENT` → `MDLL_Spawn` при необходимости); `Fake_Remove`
  (`MDLL_ClientDisconnect` + `pfnRemoveEntity` + снятие записи); `Fake_Think`
  (`pfnRunPlayerMove(ent, angles, fwd, side, up, buttons, impulse, msec=frametime)` только для
  alive); `Fake_RefreshAlive`; `Fake_OnMapStart`; `Fake_GetRecord/IsFake/Count/Indices`;
  pre-хук `GetPlayerAuthId` (`RETURN_META_VALUE(MRES_SUPERCEDE, buf)` для зарегистрированного
  фейка или pending-create с `FL_FAKECLIENT`; иначе `MRES_IGNORED`).
- **`src/FakeMethods.{h,cpp}`**: RPC `fake.create/remove/list/get/move/look/stop/buttons/set/authid`;
  регистрация как builtin в реестре; `Fake_Dispatch`; валидация params (parson); отказ на
  незарегистрированном индексе (реальные игроки); `fake.look` — `angles` или `at` (расчёт
  pitch/yaw); `fake.buttons` — имена `IN_*` или числа; `fake.set` — health/armor/team (entvars),
  weapon (`pfnClientCommand "give"`).
- **`src/moduleconfig.h`**: `#define FN_GetPlayerAuthId GetPlayerAuthId`.
- **`src/main.cpp`**: `Fake_Init(g_config.fakeMax)` в `OnAmxxAttach`; `Fake_Shutdown` в `Shutdown`;
  `Fake_Think()` в `FN_StartFrame_Post`; `Fake_OnMapStart()` в `ServerActivate`.
- **`src/CoreMethods.cpp`**: неизвестные `fake.*` делегируются в `Fake_Dispatch` (иначе `-32601`).
- **`CMakeLists.txt`**: `+ src/Fake.cpp src/FakeMethods.cpp`.
- **Доки**: `docs/04` §2 синхронизирован с `design/13` §3; `design/13` → утверждён;
  `docs/08` OQ-5 → AD-24.

## Решения и обоснование

- **Порядок authid до `pfnCreateFakeClient`** (`design/13` §3, §10a B1/M1): движок читает authid
  внутри create; запись «после create» — поздняя. `g_pendingAuthid` + pre-хук.
- **`MDLL_ClientConnect/PutInServer` обходят метамод-хуки** (`design/13` §10a B2): на регистрацию
  в AMXX не рассчитываем; её делает `ClientUserInfoChanged` внутри `pfnCreateFakeClient`. Явный
  `MDLL_ClientUserInfoChanged` не нужен.
- **`fake.*` — builtin + делегирование** (`design/13` §6): методы видны в `rpc.methods`, но
  диспетч ядра отдаёт их в `Fake_Dispatch`.
- **`map_start` — записи сбрасываются** (`Fake_OnMapStart`): дизайн (`design/13` §8) допускает
  сброс; выбран безопасный вариант (нет риска висячих edict-указателей/переиспользования слота).
  `alive` пересчитывается покадрово (`Fake_RefreshAlive` в `Fake_Think`) — смерть/респавн.
- **`fake.set` weapon** — best-effort через `pfnClientCommand("give …")`; health/armor/team —
  напрямую через entvars.
- **M2 (удаление обходит AMXX)** — принято как известный риск (`design/13` §10a M2); проверка
  stale `CPlayer` — на рантайме владельцем.

## Верификация

- Linux-модуль: exit 0; `-Wall -Wextra` — 0 предупреждений из `src/` (только вендоренный
  `amxxmodule.cpp`/hlsdk).
- `protocol_test` **54/0**; `registry_test` **60/0** (регрессия зелёная).
- Windows-DLL (zig `x86-windows-gnu` + `cmake/amxxrpc.def`): exit 0; экспорт `GiveFnptrsToDll`
  (undecorated) присутствует.
- Экспорты Linux `.so`: `GiveFnptrsToDll`, `GetPlayerAuthId`; строки `fake.*` (10 методов) в бинаре.
- **E2E на реальном HLDS-сервере** (Windows, Metamod-r + AMXX 1.10.0.5467, `scripts/e2e_fake.py`):
  - `fake.create {name:DummyOne, authid:STEAM_9:9:99999}` → `{index:3, authid:…}`;
  - `fake.list`/`fake.get` → живой клиент (`alive:true, health:100, origin:[…]`), origin между
    вызовами **меняется** (фейк реально двигается `RunPlayerMove`);
  - `fake.move`/`fake.buttons`/`fake.set`/`fake.remove` → `{ok:true}`; `fake.list` после remove — пуст;
  - `fake.get index=1` (реальный игрок) → `-32602 "index is not a registered fake"`.
- Попутно найден и исправлен баг фрейминга Этапа 1: ответы не завершались `\n` (`SendBytes`).

## Открытые нити

- **authid-override**: реализован (pre-хук), но независимо плагином (`get_user_authid`) не наблюдался —
  подтвердить на тест-сервере Pawn-проверкой.
- **M2**: удаление через `MDLL_ClientDisconnect` может оставить stale `CPlayer` в AMXX до mapchange
  (AMXX слушает `SV_DropClient` через ReHLDS).
- `map_start` сбрасывает фейков (не пересоздаёт). `fake.set` weapon/team — best-effort (mod-specific).
- Windows-сборка здесь — через zig+`.def`; штатная MSVC-сборка (CI) не прогонялась.

## Затронутые артефакты

- `src/{Fake,FakeMethods}.{h,cpp}`, `src/moduleconfig.h`, `src/main.cpp`, `src/CoreMethods.cpp`,
  `CMakeLists.txt`;
- `docs/04` §2, `docs/design/README.md`, `docs/08` (AD-24, OQ-5), `.omo/plans/0005`.

## Дальше

- **Этап 4 — YAPB-адаптер** (`design/14`, опц.): `bot.*`, деградация без YAPB.
- **Этап 5 — упаковка/поставка**.
