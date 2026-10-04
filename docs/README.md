# AmxxRpc — Техническое задание

Транспортный C++-модуль AMX Mod X для CS 1.6 (HLDS/ReHLDS), открывающий внешним
инструментам JSON-RPC-транспорт (TCP loopback + токен) для команд, дебага, чтения
состояния и управления фейк-игроками. Парная поставка — публичный Pawn-инклюд
регистрации методов.

> **Статус:** черновик ТЗ (`v0.1`). Собран из ресёрча экосистемы AMXX/Metamod/ReHLDS,
> исходников AMXX SDK, ReHLDS, ReAPI, YAPB и prior art. Развилки закрыты владельцем
> (см. [`08-open-questions.md`](08-open-questions.md) §«Принятые решения»).

## Границы проекта

- Модуль — **чистый транспорт**. Он **не** знает про MCP и **не** связан с `amxb`.
  Каталог MCP-tools и MCP-протокол — в `amxb` (вне этого репозитория). Этот репозиторий
  даёт (а) встроенное ядро RPC-методов и (б) нативы регистрации методов для Pawn-плагинов.
- Внешний мост (в `amxb`) мапит MCP-tools на RPC-методы, предоставленные модулем.

## Структура документов

| Файл | Содержание |
|---|---|
| [`00-overview.md`](00-overview.md) | Цели, область, акторы, глоссарий, среда (HLDS/ReHLDS/AMXX), карта исследований |
| [`01-architecture-requirements.md`](01-architecture-requirements.md) | Архитектурные требования: компоненты, потоки, безопасность, расширяемость |
| [`02-transport-and-protocol.md`](02-transport-and-protocol.md) | Транспорт (TCP loopback), фрейминг, JSON-RPC 2.0, токен-аутентификация |
| [`03-rpc-registry-and-core.md`](03-rpc-registry-and-core.md) | Реестр методов, встроенное ядро методов, подписки/push, нативы регистрации (Pawn) |
| [`04-fake-players.md`](04-fake-players.md) | Фейк-игроки: движковый слой, подмена authid, usercmd-слой, опциональный YAPB-адаптер |
| [`05-config-and-commands.md`](05-config-and-commands.md) | Конфиг модуля, серверные команды, логирование |
| [`06-build-and-deploy.md`](06-build-and-deploy.md) | CMake win/linux, CI, раскладка артефактов, интеграция с amxb-бандлом |
| [`07-jsonrpc-protocol.md`](07-jsonrpc-protocol.md) | **Внешний контракт JSON-RPC** (транспорт, auth, каталог методов, события) — для агента `amxb` |
| [`08-open-questions.md`](08-open-questions.md) | Допущения, открытые вопросы, риски, §«Принятые решения» |
| [`09-dependencies.md`](09-dependencies.md) | Инвентарь зависимостей (SDK/compile/runtime/optional), политика версий |
| [`design/`](design/README.md) | **Дизайн-документы** подсистем |
| [`journal/`](journal/README.md) | **Журнал разработки** (этапы, решения, обоснование) |

## Типы требований и конвенции

- `AR-NNN` — **архитектурное требование**.
- `FR-<ОБЛАСТЬ>-NNN` — **функциональное требование** (области: `TRANSPORT`, `RPC`, `FAKE`, `BOT`, `CONF`, `OBS`).
- `NFR-<КАТЕГОРИЯ>-NNN` — **нефункциональное требование** (`SEC`, `PERF`, `A11Y`→`PORT` (переносимость), `OBS`).
- `OQ-NNN` — **открытый вопрос/допущение** (`08-open-questions.md`).

Базовые соглашения:

- Модуль — транспорт, не предметная система: требования описывают транспорт, реестр,
  управление сервером и фейк-игроками, а не статистику/игровые режимы.
- Опциональные зависимости (YAPB) не должны быть обязательными ни при сборке, ни в рантайме.
- Обращение к AMXX/движку — **только из главного потока** (см. `01`, `03`).

## Как пользоваться

1. Прочитать [`00-overview.md`](00-overview.md) и [`01-architecture-requirements.md`](01-architecture-requirements.md) — рамки.
2. Транспорт/протокол — [`02`](02-transport-and-protocol.md); реестр/нативы — [`03`](03-rpc-registry-and-core.md).
3. Фейк-игроки — [`04`](04-fake-players.md); конфиг — [`05`](05-config-and-commands.md); сборка — [`06`](06-build-and-deploy.md).
4. Перед этапом закрыть вопросы из [`08-open-questions.md`](08-open-questions.md).

## Следующий этап

Стратегическая дорожная карта этапов — [`.omo/plans/0001-strategic-roadmap.md`](../.omo/plans/0001-strategic-roadmap.md);
системная архитектура — [`design/10`](design/10-system-architecture.md).
