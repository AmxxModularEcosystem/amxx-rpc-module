# 16. Конфиг и наблюдаемость (дизайн)

Статус: **черновик (на согласование)**. Область: формат конфига, reload, серверные команды,
логирование. Требования — `docs/05`, `FR-CONF-*`, `NFR-OBS-001`, `AR-009`.

## 1. Формат

- Файл `addons/amxmodx/configs/amxxrpc.cfg`, плоский `ключ = значение`; комментарии `#`/`;`.
- Читается C++-модулем (Pawn-`json` из C++ недоступен); парсер простой, fail-fast на мусоре.
- Пути — относительно игры (`get_localinfo`-эквивалент); модуль строит путь сам.

## 2. Ключи

| Ключ | Дефолт | Тип | Назначение |
|---|---|---|---|
| `host` | `127.0.0.1` | string | bind-адрес (по умолчанию loopback) |
| `port` | `27016` | int | TCP-порт |
| `token` | — | string ≥16 | общий секрет; пусто → транспорт не поднимается (fail-fast) |
| `max_connections` | `4` | int | лимит соединений |
| `idle_timeout` | `120` | int (сек) | idle-таймаут соединения |
| `pre_auth_timeout` | `5` | int (сек) | таймаут до `rpc.auth` (короче `idle_timeout`) |
| `max_message_bytes` | `1048576` | int | лимит сообщения |
| `max_session_out_bytes` | `= max_message_bytes × outbox_depth` | int | cap выходного буфера сессии (B-3); переполнение → close |
| `request_timeout` | `15` | int (сек) | таймаут неответивших запросов |
| `inbox_depth` | `256` | int | глубина `I/O→main` |
| `outbox_depth` | `256` | int | глубина `main→I/O` |
| `drain_budget` | `64` | int | запросов за кадр |
| `fake_max` | `8` | int | лимит фейков (Этап 3) |
| `yapb_path` | `` (авто) | string | путь к YAPB (Этап 4) |
| `log_level` | `info` | enum | `error`/`warn`/`info`/`debug` |

## 3. Валидация и reload

- Fail-fast (`Log` уровня error + модуль не поднимает транспорт) на: отсутствии/коротком `token`,
  невалидном `port`, отрицательных лимитах, `max_connections > FD_SETSIZE-1`.
- Неизвестные ключи — **warn + ignore** (forward-compat).
- `host` вне loopback → предупреждение (cleartext-токен, снятая изоляция; `docs/08` R-4).
- **Область live-reload:** live — `log_level`, `idle_timeout`, `request_timeout`, `drain_budget`,
  `max_connections`; требуют рестарта транспорта — `host`, `port`, `token`; `inbox_depth`/`outbox_depth` —
  при следующем старте.
- `amxxrpc_reload`: перечитать конфиг; при смене restart-ключей — **сначала `bind` нового
  listen-сокета, и только при успехе остановить старый** (join I/O-потока) и запустить новый;
  при ошибке `bind` — сохранить прежний транспорт, залогировать. Смена `token` инвалидирует сессии.

## 4. Серверные команды

`amxxrpc_reload`, `amxxrpc_status`, `amxxrpc_clients`, `amxxrpc_help`. Доступ (OQ-8 закрыт):
команды требуют админ-флага — `reload`/`status`/`clients` → `ADMIN_RCON`; `help` — без ограничений.
`status`/`clients` не показывают токен.

## 5. Наблюдаемость

- Лог-тег `AmxxRpc`; уровни по `log_level`.
- I/O-поток пишет в **файл** (`addons/amxmodx/logs/amxxrpc.log`), при необходимости — строка в
  ограниченную очередь в main (дрен в `StartFrame`); переполнение лог-очереди → дроп с пометкой.
- События: старт/стоп транспорта (host:port), accept/close, auth-fail (без токена), ошибки
  методов, переполнение очередей, состояние YAPB.
- **Потокобезопасно** и без AMXX из I/O-потока (`design/11` §7).
- Секреты — никогда (`AR-009`).

## 6. Открытые вопросы

- OQ-8 (флаги доступа к командам), OQ-1 (точный синтаксис/сортность ключей).
