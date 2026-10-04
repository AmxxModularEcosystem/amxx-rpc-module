# 14. Опциональный YAPB-адаптер (дизайн)

Статус: **утверждён (ревью пройдено)**. Область: динамическая интеграция с YAPB для навигации
(«подойти»), строго опционально; деградация без YAPB. Требования — `docs/04` §7, `docs/01`
(AR-016, FR-BOT-*), `docs/design/13` (свой usercmd-слой). Закрывает OQ-7.

## 1. Принципы

- YAPB **никогда не обязателен** (ни сборка, ни рантайм). Нет YAPB → `bot.*` дают `-32002`, модуль жив.
- Атака/выстрел — **не** через YAPB (в `IBotModule` нет attack); движение/прицел целиком умеет
  наш usercmd-слой (`design/13`). YAPB — только навигация/обход.

## 2. Загрузка (OQ-7)

- **Приоритет — уже загруженный YAPB** (Metamod-плагин): найти его модуль в процессе
  (`GetModuleHandle("yapb")` / `dlopen` с `RTLD_NOLOAD`) и резолвить `GetBotAPI`.
- Если не загружен — попытаться загрузить бинарь по `yapb_path` (Windows `addons/yapb/bin/yapb.dll`,
  Linux `addons/yapb/bin/yapb.so`); **если загрузка не регистрирует YAPB как плагин** — `GetBotAPI`
  может вернуть `nullptr`; тогда адаптер остаётся недоступным (деградация).
- `GetBotAPI(kBotModuleVersion)` возвращает `IBotModule*` (C++-vtable). **ABI-риск (R-3):** интерфейс
  append-only, но зависит от компилятора/STL (YSTL). На MSVC — совместимо, если тот же toolchain.

## 3. Используемый интерфейс `IBotModule`

| Метод | Назначение |
|---|---|
| `IsBotsInGame()` / `IsBot(ent)` / `GetBotCount()` | наличие/учёт ботов |
| `AddBot(name, difficulty, personality, team)` | добавить YAPB-бота |
| `SetBotGoalOrigin(ent, float* origin)` | «идти к точке» |
| `SetBotLookAt(ent, float* origin)` | смотреть на точку |
| `SetBotMovement(ent, bool)` | стоп/ход |
| `GetBotOrigin`/`GetBotEnemy`/`GetBotWeapon` | чтение состояния |

## 4. RPC-методы `bot.*`

| Метод | Назначение |
|---|---|
| `bot.available` | `{available: bool, version?}` |
| `bot.add` | `{name, difficulty?, personality?, team?}` → `{queued: true}` (YAPB `AddBot` returns a queued bool, no index — §9 M4) |
| `bot.list` | `[{index}]` — YAPB has no enumeration; scan player indices with `IsBot` (§9 M4) |
| `bot.goal` | `{index, origin?:[x,y,z], node?}` |
| `bot.look` | `{index, origin:[x,y,z]}` |
| `bot.freeze` | `{index, frozen: bool}` |
| `bot.status` | `{index, origin, enemy, weapon, task}` |

- Все — main thread. Нет YAPB → `-32002` (кроме `bot.available` → `{available:false}`).

## 5. Компоненты

- `src/Bot.{h,cpp}`: `Bot_Init/Shutdown`, динамическая загрузка `GetBotAPI`, `Bot_Available()`,
  обёртки над `IBotModule` (через `void*`/указатель на интерфейс; без жёсткой линковки).
- `src/BotMethods.{h,cpp}`: RPC `bot.*` + регистрация/диспатч (по образцу `FakeMethods`).
- `Config`: `yapb_path` (уже присутствует).
- `moduleconfig.h`: без изменений (adb нет).

## 6. Деградация (FR-BOT-003)

- `bot.available` → `{available:false}` (200, не ошибка).
- Прочие `bot.*` → `-32002 Service unavailable`.
- Лог: «YAPB not available; bot.* disabled».

## 7. Верификация

- Сборка 0 ошибок; off-line тесты — регрессия.
- **E2E (реальный сервер, YAPB отсутствует):** `bot.available → {available:false}`;
  `bot.add → -32002`; модуль/транспорт работают (регрессия smoke/fake).
- Путь «YAPB присутствует» — вне текущего окружения (нет YAPB) → нить владельцу.

## 8. Открытые вопросы

- OQ-7 **частично закрыт**: детект/деградация заданы; ABI (R-3) — при наличии YAPB.
- Нужно ли поддерживать YAPB `SetBotGoal` (nav-node) помимо origin — да, опц.

## 9. Ревизия после ревью (резолвы)

| # | Проблема | Решение |
|---|---|---|
| B1 | Кэш `IBotModule*` **dangling** при reload YAPB на mapchange | **Не кэшировать через mapchange.** `yapb_amxx` имеет reload-on-mapchange и выгружает ядро. Резолвить лениво/на `map_start`; хранить per-map generation; при `map_start` — сброс указателя и повторный резолв. |
| B2 | `GetBotAPI` возвращает **ненулевой** singleton всегда → ложная доступность | `available` = **ядро YAPB уже загружено** (детект модуля в процессе) **и** `GetBotAPI` резолвится. **Не** self-load по умолчанию (загрузка как plain-lib не регистрирует движок → вызовы бьют по неинициализированным глобалам). Self-load — опц., за конфигом, с документированным риском. |
| M3 | Цель детекта — **AMXX-модуль** `yapb_amxx`, грузящий ядро `yapb.{so,dll}` (не «Metamod-плагин»); Linux `dlopen("yapb.so",RTLD_NOLOAD)` не сработает (нет SONAME) | Кандидаты: Win `GetModuleHandleA("yapb.dll")`; Linux `dlopen` по полному пути (`addons/yapb/bin/yapb.so`) / `dl_iterate_phdr` по basename. Путь — от modname, не от cwd. Детект `yapb_amxx` — приоритетный сигнал доступности. |
| M4 | `bot.add` не может вернуть `{index}` (YAPB `AddBot` → queued bool) | `bot.add` → `{queued:true}`; индекс искать через `bot.list`/`players.list`. Добавить `bot.list`. |
| M5 | Нет `IsBot`-гарда на index-методах | Каждый `bot.*` с индексом проверяет `IsBot(ent)`; иначе `-32602` (не управлять реальными игроками). |
| M6 | Обёртки нетестируемы без YAPB | Тонкий seam (таблица указателей/mock `IBotModule`); off-line тест: `available:false`, `-32002`, валидация params, отказ по `IsBot`. |
| M7 | OQ-7 overclaim | «Частично закрыт» (см. §8); R-3 открыт. |
| M8 | Владение библиотекой при shutdown | `Bot_Init` помнит, грузил ли сам; `Bot_Shutdown` выгружает только self-loaded. |
| M9 | Гигиена интерфейса | Не вызывать виртуальный деструктор/`delete`; в интерфейсе нет STL; `float*` копировать сразу. |
| M10 | Путь по умолчанию | `yapb_path` (уже есть); default — от modname. |

Уточнения §4: `bot.status` использует `GetBotWeapon`/`GetBotTask` (обёртки обязательны); `bot.available → {available, version?}`.
