# 0006. Этап 4 — Опциональный YAPB-адаптер

**Дата:** 2026-10-04 · **Этап:** 4 YAPB-адаптер · **Статус:** реализовано; сборка Linux/Windows +
off-line тесты зелёные; **e2e деградации на реальном HLDS без YAPB — 15/0**; регрессия smoke (12/0)
и fake e2e — зелёные; presence-путь (YAPB установлен) — вне окружения

## Контекст

Пятый этап дорожной карты, поверх транспорта (Этап 1), реестра/Pawn-API (Этап 2) и фейк-игроков
(Этап 3). Дизайн — `docs/design/14` (§1–9, включая резолвы ревью §9), план — `.omo/plans/0006`.
Требования — `docs/01` (AR-016, FR-BOT-001…003), `docs/04` §7, `docs/09` §2. Закрывает OQ-7.

Ключевые ограничения из ревью (`design/14` §9): `GetBotAPI != nullptr` — **не** признак готовности
(B2); `IBotModule*` **нельзя** кэшировать через mapchange (B1) и **нельзя** удалять (M9); YAPB
никогда не обязателен (деградация `-32002`).

## Сделано

- **`third_party/yapb/module.h`** — вендоренный `IBotModule` (commit `ac7e4e75…`, Unlicense);
  строка в `third_party/VERSIONS.md`.
- **`src/Bot.{h,cpp}`**:
  - тонкий seam `IBotApi` (подмножество `IBotModule`) + адаптер `BotModuleApi` поверх реального
    `bot::IBotModule` (M6); mock инжектится через `Bot_SetApiForTest`;
  - детект ядра: Win `GetModuleHandleA("yapb.dll")`; Linux `dl_iterate_phdr` по basename `yapb.so`
    + `dlopen(RTLD_NOLOAD)` по полному пути (M3); `GetBotAPI(kBotModuleVersion)` резолвится только
    после детекта (B2);
  - self-load — только при `yapb_self_load=true` (по умолчанию off), ownership-флаг
    `g_selfLoaded`/`g_libraryHandle`; `Bot_Shutdown` выгружает только self-loaded (M8);
  - `Bot_OnMapStart` сбрасывает указатель; `Bot_Available`/`Bot_Version` резолвят лениво (B1);
  - обёртки `Bot_IsBotsInGame/IsBot/GetBotCount/AddBot/SetBotGoal/SetBotGoalOrigin/SetBotLookAt/
    SetBotMovement/GetBotOrigin/GetBotEnemy/GetBotWeapon/GetBotTask`;
  - гардированные операции `Bot_Add/Goal/Look/Freeze/Status` с кодами ошибок (тестируемы off-line).
- **`src/BotMethods.{h,cpp}`** — RPC `bot.available/add/list/goal/look/freeze/status`; регистрация
  как builtin; `Bot_Dispatch`; без YAPB → `-32002` (кроме `bot.available → {available:false}`);
  `IsBot`-гард (`-32602`); `bot.add → {queued:true}`; `bot.list` — скан индексов 1..maxClients
  через `IsBot`; `bot.goal` — `origin` или `node`; `bot.status` — origin/enemy/weapon/task.
- **`src/Config.{h,cpp}` + `configs/amxxrpc.cfg`** — ключ `yapb_self_load` (default false).
- **`src/main.cpp`** — `Bot_Init(YapbPath(), yapbSelfLoad)` в attach; `Bot_Shutdown` в shutdown;
  `Bot_OnMapStart` в `ServerActivate`; `CmdStatus` печатает YAPB-доступность; лог
  «YAPB not available; bot.* disabled».
- **`src/CoreMethods.cpp`** — `bot.*` делегируются в `Bot_Dispatch`.
- **`CMakeLists.txt`** — `+ src/Bot.cpp src/BotMethods.cpp`, include `third_party/yapb`, таргет
  `bot_test`.
- **`tests/bot_test.cpp`** — mock `IBotApi`: деградация, валидация params, отказ `IsBot`,
  маппинг ошибок, успешные пути, сброс на map_start.
- **`scripts/e2e_bot.py`** — auth → `bot.available == {available:false}`; `bot.add/goal/look/
  freeze/status/list → -32002`; `rpc.methods` содержит `bot.*`.
- **Доки** — `design/14` → утверждён (README); `docs/08` OQ-7 → AD-25, §3 пуст.

## Решения и обоснование

- **Доступность = детект ядра + резолв `GetBotAPI`** (`design/14` §9 B2): `GetBotAPI` возвращает
  ненулевой singleton безусловно; без детекта ядра вызовы бьют по неинициализированным глобалам.
- **Не кэшировать через mapchange** (B1): `yapb_amxx` reload-on-mapchange выгружает ядро; указатель
  сбрасывается в `ServerActivate`, резолв ленивый.
- **Self-load за конфигом** (B2/M8): по умолчанию off; ownership-флаг гарантирует, что чужую
  библиотеку не выгружаем.
- **`bot.add → {queued:true}`** (M4): YAPB `AddBot` возвращает bool очереди, не индекс; индекс —
  через `bot.list`/`players.list`.
- **`IsBot`-гард** (M5): index-методы отвергают не-ботов `-32602`; диапазон индекса проверяется до
  вызова YAPB (защита от чтения вне таблицы ботов).
- **Seam `IBotApi`** (M6): off-line тесты без YAPB; адаптер не удаляет `IBotModule` (M9).
- **`bot.goal` node** (`design/14` §8): поддержан через `SetBotGoal` (доп. обёртка сверх списка
  плана — см. «Отклонения»).

## Верификация

- Linux-модуль: exit 0; `-Wall -Wextra` — **0 предупреждений из `src/`** (247 — вендоренный
  `amxxmodule.cpp`/hlsdk/ITextParsers).
- `protocol_test` **54/0**; `registry_test` **60/0**; `bot_test` **62/0**.
- Windows-DLL (zig `x86-windows-gnu` + `cmake/amxxrpc.def`): exit 0; 0 ошибок; 0 предупреждений из
  `src/`; экспорт `GiveFnptrsToDll` присутствует.
- Linux `.so`: строки `bot.available/add/list/goal/look/freeze/status` в бинаре.
- **E2E деградации** (`scripts/e2e_bot.py`, реальный HLDS без YAPB, Windows python): **15/0** —
  `bot.available → {available:false}`; `bot.add/goal/look/freeze/status/list → -32002`;
  `rpc.methods` содержит все `bot.*`.
- **Регрессия**: `smoke_tcp.py` **12/0**; `e2e_fake.py` — create/list/get/move/buttons/set/remove
  работают, реальный игрок отвергнут `-32602`.

## Отклонения от плана

- Добавлена обёртка `SetBotGoal` (nav-node) сверх перечня плана — требуется `design/14` §4/§8
  (`bot.goal {node?}`).
- Добавлен конфиг-ключ `yapb_self_load` (default false) — требуется резолвом B2 для гейта self-load.
- `bot.list` возвращает `[{index}]` (в `design/14` §4 таблица дополнена).

## Открытые нити

- **Presence-путь (YAPB установлен)** — вне окружения: ABI `IBotModule` (R-3), реальные
  `AddBot`/`SetBotGoalOrigin`/`IsBot` не проверялись.
- Windows-сборка здесь — zig+`.def`; штатная MSVC-сборка (CI) не прогонялась.

## Затронутые артефакты

- `third_party/yapb/module.h`, `third_party/VERSIONS.md`;
- `src/{Bot,BotMethods}.{h,cpp}`, `src/Config.{h,cpp}`, `src/main.cpp`, `src/CoreMethods.cpp`,
  `CMakeLists.txt`, `configs/amxxrpc.cfg`;
- `tests/bot_test.cpp`, `scripts/e2e_bot.py`;
- `docs/design/14`, `docs/design/README.md`, `docs/08` (AD-25, OQ-7), `.omo/plans/0006`.

## Дальше

- **Этап 5 — упаковка/поставка** (раскладка `addons/`, инклюд, пример, CI).
