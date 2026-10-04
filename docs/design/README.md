# Дизайн-документы AmxxRpc

Технические решения по подсистемам. Порождаются в ходе проектирования и ссылаются на ID
требований из `../` (ТЗ). Статус каждого документа — в его шапке.

| Док | Тема | Статус |
|---|---|---|
| [`10-system-architecture.md`](10-system-architecture.md) | Системная архитектура: компоненты, потоки, границы, безопасность, дорожная карта | черновик (на согласование) |
| [`15-build-and-toolchain.md`](15-build-and-toolchain.md) | CMake, вендоринг SDK, CI-матрица, раскладка | утверждён (ревью пройдено) |

## Ожидается (по этапам)

| Док | Тема | Этап |
|---|---|---|
| `11-transport-and-protocol.md` | TCP-сервер, фрейминг, JSON-RPC 2.0, auth, таймауты | Транспорт |
| `12-rpc-registry-and-pawn-api.md` | Реестр методов, диспетчер, Pawn-нативы, формат requestId, батчи | RPC/Pawn |
| `13-fake-players.md` | Движковый слой, `FakeRecord`, usercmd/think, подмена authid, RPC `fake.*` | Фейк-игроки |
| `14-bot-adapter-yapb.md` | Динамическая загрузка YAPB, `IBotModule`, деградация | Боты (опц.) |
| `16-config-and-observability.md` | Формат конфига, reload, команды, логи | Конфиг |

## Справочное

- [`../01-architecture-requirements.md`](../01-architecture-requirements.md) — требования.
- [`../08-open-questions.md`](../08-open-questions.md) — открытые вопросы и решения.
