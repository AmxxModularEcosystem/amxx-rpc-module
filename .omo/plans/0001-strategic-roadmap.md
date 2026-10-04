# Work Plan: AmxxRpc — стратегическая дорожная карта

## Profile bindings (carried from AGENTS.md)

- **Ревьюер дизайна:** `oracle`. **Ревьюер плана:** `momus`.
- **Дизайн-доки:** `docs/design/`, индекс `docs/design/README.md`.
- **Планы:** `.omo/plans/`. **Журнал:** `docs/journal/`, индекс `docs/journal/README.md`.
- **Open questions / решения:** `docs/08-open-questions.md` §1/§3.
- **Верификация этапа:** CMake build win/linux 0 ошибок; загрузка модуля в HLDS без ошибок;
  smoke JSON-RPC (`rpc.auth`+`rpc.ping`/`rpc.version`) по TCP; `amxxpc 1.10.5428` 0/0 для
  инклюда/примера.
- **Политика коммитов:** только по явному указанию владельца, одним связным коммитом на этап.

## Owner answers (AD-1…AD-8, docs/08 §1)

1. Модуль — чистый транспорт; MCP — в `amxb`.
2. Ядро методов + нативы регистрации Pawn.
3. Фейки: свой usercmd-слой + опциональный YAPB (навигация); zBot не используется.
4. Сборка — CMake (win/linux).
5. TCP loopback + токен.
6. Имя — AmxxRpc.
7. `USE_METAMOD` обязателен.
8. Двухпоточная модель; AMXX/движок — только главный поток.

## Goal

Довести AmxxRpc до этапа hardening: загружаемый C++-модуль, дающий внешнему инструменту
JSON-RPC-доступ к серверу (команды, состояние, фейк-игроки) с регистрацией методов из Pawn.

## Источник истины

- `docs/00` (обзор), `docs/01` (AR/FR/NFR), `docs/02`–`docs/06` (контракты), `docs/08` (OQ/риски),
  `docs/09` (зависимости), `docs/design/10` (архитектура), `AGENTS.md` (конвенции).
  Ресёрч — журнал `docs/journal/0001`.

## Этапы

Каждый этап проходит цикл `feature-delivery` (дизайн → ревью дизайна → [вопросы владельцу] →
план → ревью плана → реализация → запись). **Гейт реализации — после ревью дизайна и плана
этапа.** Дизайн-документ этапа — его prerequisite.

| Этап | Дизайн-гейт | Закрывает OQ |
|---|---|---|
| 0 Bootstrap | `design/15` | OQ-9, OQ-16 |
| 1 Транспорт | `design/11`, (`design/16`) | OQ-1, OQ-6, OQ-8, OQ-11, OQ-12, OQ-13, OQ-15, OQ-17 |
| 2 Реестр/Pawn | `design/12` | OQ-2, OQ-3, OQ-4, OQ-10, OQ-14 |
| 3 Фейк-игроки | `design/13` | OQ-5 |
| 4 Боты (опц.) | `design/14` | OQ-7 |
| 5 Hardening | — | — |

### Этап 0 — Bootstrap (репо, SDK, CMake, CI, пустой модуль)

- **Цель:** каркас сборки и загружаемый модуль-пустышка.
- **Границы:** без транспорта/RPC/фейков. Не трогаем SDK (кроме `moduleconfig.h`).
- **Deliverables:** структура `src/`, `sdk/` (вендор SDK + metamod/hlsdk), `CMakeLists.txt`,
  `.github/workflows`, `moduleconfig.h` (`USE_METAMOD`), `main.cpp` (`OnAmxxAttach/Detach` +
  логирование версии), `include/AmxxRpc/Core.inc` (версия), `examples/` (пустой плагин),
  `docs/conventions/{cpp-module.md,amxx-pawn.md}` (конвенции, на которые ссылается AGENTS.md).
- **Verification (точные команды):** `<win>: cmake -S . -B build-win -A Win32 && cmake --build build-win --config Release`;
  `<linux>: cmake -S . -B build-lin -DCMAKE_CXX_FLAGS=-m32 && cmake --build build-lin`;
  загрузка `amxxrpc_amxx_i386.so` в HLDS (лог без ошибок); `amxxpc include/AmxxRpc/Core.inc` + пример → 0/0.
- **Acceptance:** модуль собирается win/linux; грузится в HLDS и логирует версию; инклюд и
  пример компилируются `0/0`; `modules.ini` активирует `amxxrpc`.
- **Verification:** CMake win+linux; загрузка в HLDS; `amxxpc`.
- **Risks:** ABI экспорта `GiveFnptrsToDll` (MSVC), совместимость glibc.

### Этап 1 — Транспорт

- **Цель:** TCP loopback + фрейминг + auth + I/O-поток + очередь + StartFrame-дренаж; конфиг.
- **Границы:** без реестра/плагинов/фейков. Методы ограничены `rpc.auth`/`rpc.ping`/`rpc.version`.
- **Deliverables:** `Config_*`, `Transport_*`, `Protocol_*` (фрейминг+JSON-RPC), `Queue_*`,
  I/O-поток, `StartFrame`-дренаж, конфиг `amxxrpc.cfg`, команды `amxxrpc_reload/status`.
- **Acceptance:** `rpc.auth` с верным/неверным токеном (`-32001` + close на неверном);
  `rpc.ping`/`rpc.version`; соблюдение `max_connections`/`idle_timeout`/`max_message_bytes`/
  `inbox_depth` (наблюдаемо: отказ `-32004` при переполнении); корректный join на выгрузке
  (без зависания); при отсутствии трафика накладные расходы StartFrame пренебрежимы
  (`NFR-PERF-002`) — фиксируется замером/логом.
- **Verification:** smoke-клиент по TCP (`docs/06` §5); лог HLDS; сборка win/linux (точные
  команды — как в Этапе 0).
- **Risks:** гонки (митигация — модель `design/10` §4); неблокирующий I/O.

### Этап 2 — Реестр методов и Pawn-API

- **Цель:** диспетчер методов + встроенное ядро + нативы регистрации.
- **Границы:** без фейков/ботов.
- **Deliverables:** `Rpc_*` (реестр/диспетчер), core-методы (`server.exec`, `cvar.*`,
  `players.list`/`players.get`, `rpc.methods`, `events.subscribe`/`unsubscribe`, `logs.tail`),
  Pawn-нативы (`ARpc_Core_*`), пример плагина с методом.
- **Acceptance:** плагин регистрирует метод и получает ответ; core-методы работают; ошибки по
  кодам `docs/02` §4; таймаут неответившего.
- **Verification:** smoke + пример-плагин `0/0`; сборка win/linux.
- **Risks:** диспетч Pawn (OQ-2), requestId/async (OQ-3), JSON-либа (OQ-4).

### Этап 3 — Фейк-игроки

- **Цель:** движковый слой + подмена authid + usercmd-think + `fake.*`.
- **Границы:** без YAPB.
- **Deliverables:** `Fake_*` (`FakeRecord`, create/remove, think), хук `FN_GetPlayerAuthId`,
  методы `fake.*`.
- **Acceptance:** фейк создаётся (слот, флаги), имеет заданный authid, двигается/смотрит/
  стреляет по `fake.*`, корректно удаляется; think не ломает сервер.
- **Verification:** e2e на тест-сервере (спавн/движение/выстрел/authid); сборка win/linux.
- **Risks:** покадровый драйв (bbox), слоты, хук authid (R-2).

### Этап 4 — Опциональный YAPB-адаптер

- **Цель:** навигация/«подойти» через YAPB; деградация без него.
- **Границы:** YAPB не обязателен; attack — своим usercmd-слоем (не YAPB).
- **Deliverables:** `Bot_*` (dlopen `GetBotAPI`, `bot.available/add/goal/look/freeze`).
- **Acceptance:** при наличии YAPB бот идёт к точке/смотрит; при отсутствии — `-32002`, модуль жив.
- **Verification:** e2e с/без YAPB; сборка win/linux.
- **Prerequisite:** тест-сервер с установленным YAPB (AS-1 подтверждает только
  ReHLDS+Metamod+AMXX); иначе сценарий «с YAPB» условный — фиксируется способ симуляции
  отсутствия/наличия.
- **Risks:** ABI YAPB (R-3), путь/детект/`dlopen`-семантика (OQ-7, R-10).

### Этап 5 — Hardening

- **Цель:** поставка, пример, e2e-харнесс, документация.
- **Границы:** **без новой функциональности** — только упаковка, примеры/харнесс, документация,
  обновление журнала; исправления по итогам e2e допустимы, новые подсистемы — нет.
- **Deliverables:** примеры сценариев, тест-харнесс (`tests/`), упаковка `addons/amxmodx/...`
  (точные пути — `docs/06` §3), обновлённые доки/журнал; опц. хук интеграции с amxb-бандлом.
- **Acceptance (верифицируемо):** CI-матрица зелёная на обеих ОС; архив содержит точные пути
  из `docs/06` §3; `amxxpc 0/0` для инклюда/примера; e2e-скрипт завершается кодом 0; журнал
  содержит запись этапа и индекс обновлён.
- **Verification:** CI (win+linux); e2e-скрипт (exit 0); распаковка архива и сверка путей.

## MUST NOT

- Реализовывать MCP/каталог tools в модуле (AD-1).
- Обращаться к AMXX/движку из I/O-потока.
- Блокирующий `recv`/`accept` на главном потоке (только неблокирующий I/O + очередь).
- Вводить обязательную зависимость от YAPB.
- Управлять реальными игроками через `fake.*` или превышать `fake_max`.
- Логировать токен.
- Редактировать вендоренный SDK (`amxxmodule.*`).
- Переопределять core-методы (`rpc.*`, `server.exec`, `cvar.*`) из плагина.

## Out of scope

- MCP-мост и MCP-tools (в `amxb`).
- Игровая предметная логика (в Pawn-плагинах).
- Программный контроль zBot через Orpheu/интерналы.

## Risks / notes

- Порядок этапов обязателен: транспорт → реестр → фейки → боты → hardening.
- Этапы 0–1 можно частично параллелить по подзадачам (SDK/CMake vs транспорт), но гейт
  реализации — после ревью дизайна/плана соответствующего этапа.
- **Каждый постадийный план ОБЯЗАН зафиксировать точные команды верификации** (канон —
  `docs/06` §5); в этом roadmap команды даны для этапов 0–1.
