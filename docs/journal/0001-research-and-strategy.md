# 0001. Ресёрч экосистемы + стратегическое планирование

**Дата:** 2026-10-04 · **Этап:** ресёрч + стратегическое планирование · **Статус:** ресёрч завершён,
стратегия провалидирована (ревью пройдено, правки внесены)

## Контекст

Задача: дать AI-агенту доступ к живому серверу CS 1.6 (команды, дебаг, состояние,
управление фейк-игроками). Выбран транспорт — C++-модуль AMXX с JSON-RPC по TCP, внешний
MCP-мост отдельно (в `amxb`). Требовалось: понять, что реализуемо в среде HLDS/AMXX;
выбрать архитектуру; договориться с владельцем по развилкам; заложить framework документации
(по образцу `../ExtendedStats`). Работа велась в чистом репозитории (до этого — только `.codegraph`).

## Сделано

- **Ресёрч (2 захода, 7 фоновых `librarian` + локальные исходники AMXX SDK).** Темы:
  1. AMXX C++ module SDK / ABI / сборка / потоки.
  2. Фейк-игроки (движковый `CreateFakeClient`, authid, YAPB/PodBot, ReGameDLL zBot, ReAPI).
  3. Сетевой сервер внутри модуля (потоки, libmicrohttpd, prior art).
  4. MCP-мосты и prior art управления игровыми серверами.
  5. Подмена authid + создание фейк-клиента.
  6. Программный контроль ботов (zBot / ReAPI / YAPB).
  7. CMake-сборка AMXX-модулей (prior art, флаги, CI).
- **Обсуждение с владельцем**, закрыты 7 развилок (см. `docs/08` §1, AD-1…AD-7).
- **Каркас репозитория и документации** развёрнут по образцу ExtendedStats: `AGENTS.md`
  (профиль + конвенции), `docs/` (README, 00–06, 08, 09), `docs/design/` (README, 10),
  `docs/journal/` (README, 0001), `.omo/plans/`.

## Решения и обоснование (ключевые факты ресёрча)

- **AMXX однопоточный.** В ядре нет блокировок вокруг `executeForwards`; создание
  `std::thread` допустимо только для чистого I/O, AMXX из чужого потока — гонки
  (`webserver_amxx`, `mschnitzer/sockets` — анти-примеры). Эталон — `mysqlx`: worker-поток +
  `StartFrame`-дренаж. → **двухпоточная модель** (`AR-001…006`, `design/10` §4, AD-8).
- **`USE_METAMOD` обязателен** (`AD-7`): нужны хуки `StartFrame` (дренаж), `GetPlayerAuthId`
  (authid), `CreateFakeClient`.
- **Фейк-клиент** — `pfnCreateFakeClient` + `MDLL_ClientConnect`/`ClientPutInServer` +
  флаги `FL_FAKECLIENT|FL_CLIENT`; покадровый `RunPlayerMove`, иначе «призрак». Занимает
  клиент-слот (`AR-014`, `docs/04`).
- **authid у фейков = `"BOT"`** (движок). ReHLDS/ReAPI хука авторида **не** дают
  (`PF_GetPlayerAuthId` жёстко `"BOT"` при `cl->fakeclient`). Подмена — движковый хук
  `FN_GetPlayerAuthId` через Metamod engine-function table; канонический приём — yapb
  `src/linkage.cpp` (`RETURN_META_VALUE(MRES_SUPERCEDE, …)`). → `AR-015`, `docs/04` §3.
- **Управление ботами:** zBot (ReGameDLL) **без стабильного API** (только Orpheu/server-cmd);
  YAPB даёт стабильный `IBotModule` (`GetBotAPI`: `AddBot`, `SetBotGoalOrigin`, `SetBotLookAt`,
  `SetBotMovement`) — но **без attack**; ReAPI — только `RG_CBotManager_OnEvent` + базовые
  поля. → **гибрид**: свой usercmd-слой (движение/прицел/стрельба) + опциональный YAPB для
  навигации (AD-3, `docs/04` §4/§7).
- **CMake** — обильный prior art (ReAPI, AmxxEasyHttp, fastdl_mm, addtofullpack_manager):
  `-m32`, `-static-libstdc++`, `-fcf-protection=none`, `HAVE_STDINT_H`, Windows `ws2_32+wsock32`,
  `/EXPORT:GiveFnptrsToDll`, имена `_amxx.dll`/`_amxx_i386.so` (AD-4, `docs/06`).
- **Транспорт** — TCP loopback + токен; фрейминг — строка-JSON (`\n`), совместимо с
  MCP-stdio и byte-pump мостом (`rbash-mcp`) (AD-5, `docs/02`).
- **Границы**: модуль — чистый транспорт; MCP и каталог tools — в `amxb` (AD-1).

## Валидация (ревью) и внесённые правки

- **Ревью дизайна (`oracle`)** и **ревью плана (`momus`)** — независимо, параллельно.
  План: **OKAY** (блокеров нет) с major-замечаниями; дизайн — разбор с архитектурными пробелами.
- Внесены правки:
  - **`design/10` §4** — резолв модели потоков: один I/O-поток + неблокирующий мультисокет,
    входные/выходные буферы (частичная запись), backpressure (ответы не дропаются, нотификации —
    первыми; переполнение → `-32004`), shutdown-wakeup + join, бюджеты/глубины очередей,
    requestId/async-семантика; §7 — решения 7.8–7.10 (один I/O-поток, `server.exec` = RCE-граница,
    запрет переопределения core-методов); §8 — защита реальных игроков.
  - **`docs/02`** — коды `-32004`/`-32005`, pre-auth таймаут, частичная запись, порядок ответов.
  - **`docs/01`** — fix опечаток; AR-010 (request-timeout/очереди); FR-RPC-008/009,
    FR-FAKE-009/010/011.
  - **`docs/03`** — политика коллизий (плагин не переопределяет core), `rpc.auth` = транспортный
    уровень; §5а (захват вывода `server.exec`), §5б (валидация reply, notifications).
  - **`docs/04`** — порядок authid до `ClientConnect`; удаление/лимит/защита реальных игроков;
    `RunPlayerMove` каждый кадр с `msec=frametime`.
  - **`docs/05`** — ключи очередей/`drain_budget`/`fake_max`, min-длина токена, warning вне loopback;
    удалён `fake_think_interval`.
  - **`docs/08`** — OQ-12…OQ-17, риски R-5…R-11.
  - **`.omo/plans/0001`** — таблица «этап → design-гейт → OQ», точные команды верификации,
    Границы+верифицируемое acceptance Этапа 5, prerequisite YAPB, расширенные MUST NOT.
  - **`docs/conventions/{cpp-module.md,amxx-pawn.md}`** — созданы (ссылки из AGENTS.md).

## Открытые нити

- OQ-1…OQ-11 (`docs/08` §3): формат конфига/reload, диспетч Pawn-обработчиков, requestId/async,
  JSON-библиотека, представление фейка, батчи, детект/ABI YAPB, доступ к командам, вендоринг SDK,
  состав поставки.
- **Валидация** стратегии (ревью дизайна/плана) — следующий шаг.
- Физическое переименование папки `amxb-server-mcp → amxx-rpc` **не удалось** (ОС-лок
  открытого редактора держит cwd на Windows-ФС); выполнить после закрытия сессии. Продукт
  везде назван `AmxxRpc`.

## Затронутые артефакты

- `AGENTS.md`; `docs/README.md`, `docs/00…06`, `docs/08`, `docs/09`;
- `docs/design/README.md`, `docs/design/10-system-architecture.md`;
- `docs/journal/README.md`, `docs/journal/0001-research-and-strategy.md`;
- `.omo/plans/0001-strategic-roadmap.md`.

## Дальше

- Гейт реализации открыт (дизайн и план отревьюены). Старт **Этапа 0 (Bootstrap)** по циклу
  `feature-delivery`: дизайн `design/15`+`design/16` → ревью → план этапа → ревью → реализация.
- Перед стартом владелец подтверждает переход к реализации (коммит — только по явному указанию).
