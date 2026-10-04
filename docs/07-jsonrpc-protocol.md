# 07. JSON-RPC протокол (внешний контракт)

> **Назначение.** Это **стабильный wire-контракт** модуля AmxxRpc для внешних инструментов
> (в первую очередь MCP-моста в `amxb`). Мост мапит MCP-tools на RPC-методы, описанные здесь.
> Модуль — чистый транспорт: он не знает про MCP. Реализация — `design/11` (транспорт/кодек),
> `design/12` (реестр/ядро), `design/13` (`fake.*`), `design/14` (`bot.*`).
>
> **Версия протокола:** `ARP_PROTO_VERSION` (возвращается `rpc.version`).

## 1. Транспорт

- **TCP**, по умолчанию `127.0.0.1:27016` (настраивается `host`/`port` в конфиге модуля).
- **Фрейминг:** одно JSON-сообщение на строку, разделитель `\n` (LF); UTF-8; сообщение **не**
  содержит встроенных переводов строк. Максимальный размер — `max_message_bytes`.
- Клиент может держать **несколько** соединений (лимит `max_connections`).
- Сервер шлёт **ответы** и **push-нотификации** в одном и том же фрейминге.

## 2. Аутентификация

- Первое сообщение соединения **обязано** быть запросом `rpc.auth` с токеном:

```json
{"jsonrpc":"2.0","id":1,"method":"rpc.auth","params":{"token":"<secret>"}}
```

- Успех:

```json
{"jsonrpc":"2.0","id":1,"result":{"ok":true}}
```

- Любое иное сообщение **до** аутентификации → ошибка `-32001` (`not authenticated`) и **закрытие**
  соединения. Неверный токен → `-32001` + закрытие.
- Токен не логируется и не отражается в ответах.

## 3. Формат JSON-RPC 2.0

- Запрос: `{"jsonrpc":"2.0","id":<id>,"method":<string>,"params":<object|array|absent>}`.
- Ответ: `{"jsonrpc":"2.0","id":<id>,"result":<any>}` или
  `{"jsonrpc":"2.0","id":<id>,"error":{"code":<int>,"message":<string>}}`.
- Нотификация (без `id`) обрабатывается, ответа не получает.
- `id` — число/строка/`null`; **эхо-возвращается дословно** (сопоставляйте ответ по `id`).
- Батчи (JSON-массив верхнего уровня) **не поддерживаются** → ошибка `-32600`.

### Коды ошибок

| Код | Значение |
|---|---|
| `-32700` | Parse error (невалидный JSON) |
| `-32600` | Invalid Request |
| `-32601` | Method not found |
| `-32602` | Invalid params |
| `-32603` | Internal error |
| `-32001` | Not authenticated (терминальная: далее close) |
| `-32002` | Service unavailable (напр. YAPB отсутствует) |
| `-32003` | Engine/AMXX error |
| `-32004` | Server busy (переполнение очереди) |
| `-32005` | Request timeout (неответивший запрос) |

## 4. Каталог методов

Полный и актуальный список — `rpc.methods` (включая методы, зарегистрированные Pawn-плагинами):

```json
{"jsonrpc":"2.0","id":2,"method":"rpc.methods"}
→ {"result":[{"name":"rpc.ping","source":"builtin","description":"liveness check"}, ...]}
```

`source` ∈ `builtin` | `pawn` | `transport`.

### 4.1 Ядро

| Метод | Params | Result |
|---|---|---|
| `rpc.auth` | `{token}` | `{ok:true}` (транспортный, вне реестра) |
| `rpc.ping` | — | `{pong:true}` |
| `rpc.version` | — | `{module, protocol}` |
| `rpc.methods` | — | `[{name, source, description}]` |
| `server.exec` | `{command}` | `{ok:true}` (вывод команды не захватывается) |
| `cvar.get` | `{name}` | `{name, value}` |
| `cvar.set` | `{name, value}` | `{name, value}` |
| `players.list` | — | `[{index, name, authid, team, health, origin:[x,y,z]}]` |
| `players.get` | `{index}` | `{index, name, authid, team, health, origin:[x,y,z]}` |
| `events.subscribe` | `{event}` | `{ok:true}` |
| `events.unsubscribe` | `{event}` | `{ok:true}` |

### 4.2 Фейк-игроки (`fake.*`)

Создаются движковые fake-client с подменённым `authid`. Индекс — как индекс игрока (1..maxClients);
методы отвергают незарегистрированные индексы (реальных игроков) ошибкой `-32602`.

| Метод | Params | Result |
|---|---|---|
| `fake.create` | `{name, authid?, team?}` | `{index, authid}` |
| `fake.remove` | `{index}` | `{ok:true}` |
| `fake.list` | — | `[{index, name, authid, alive, health, armor, team, origin, angles}]` |
| `fake.get` | `{index}` | `{index, name, authid, alive, health, armor, team, origin, angles}` |
| `fake.move` | `{index, forward?, side?, up?}` | `{ok:true}` |
| `fake.look` | `{index, angles?:[p,y,r], at?:[x,y,z]}` | `{ok:true}` (нужен `angles` или `at`) |
| `fake.stop` | `{index}` | `{ok:true}` |
| `fake.buttons` | `{index, press?:[...], release?:[...]}` | `{ok:true}` |
| `fake.set` | `{index, health?, armor?, team?, weapon?}` | `{ok:true}` |
| `fake.authid` | `{index, authid?}` | `{index, authid}` |

- `press`/`release` — имена кнопок (`IN_ATTACK`, `IN_JUMP`, `IN_DUCK`, `IN_FORWARD`, `IN_BACK`,
  `IN_USE`, `IN_CANCEL`, `IN_LEFT`, `IN_RIGHT`, `IN_MOVELEFT`, `IN_MOVERIGHT`, `IN_ATTACK2`,
  `IN_RUN`, `IN_RELOAD`, `IN_ALT1`, `IN_SCORE`) или числа.
- Лимит числа фейков — `fake_max`; превышение → `-32004`.
- Фейки занимают реальные клиент-слоты и **видны AMXX** как боты (`get_players`), но с заданным authid.

### 4.3 Боты YAPB (`bot.*`, опционально)

Требуют установленный YAPB. Без YAPB: `bot.available → {available:false}`, прочие → `-32002`.

| Метод | Params | Result |
|---|---|---|
| `bot.available` | — | `{available, version?}` |
| `bot.add` | `{name, difficulty?, personality?, team?}` | `{queued:true}` (индекс асинхронный) |
| `bot.list` | — | `[{index}]` |
| `bot.goal` | `{index, origin?:[x,y,z], node?}` | `{ok:true}` |
| `bot.look` | `{index, origin:[x,y,z]}` | `{ok:true}` |
| `bot.freeze` | `{index, frozen:bool}` | `{ok:true}` |
| `bot.status` | `{index}` | `{index, origin, has_origin, enemy, weapon, task}` |

- `bot.add` возвращает `queued` (YAPB ставит бота в очередь); индекс ищите через `bot.list`/`players.list`.
- Атака/выстрел через YAPB не управляется — используйте `fake.*`/usercmd (или `bot.look` + внешний слой).

## 5. Push-нотификации (события)

- После `events.subscribe {event}` сессия получает нотификации:
  `{"jsonrpc":"2.0","method":"<event>","params":<payload>}` (без `id`).
- Встроенные события:
  - `player_connect` — `{name, address}`;
  - `player_disconnect` — `{name, authid}`;
  - `map_start` — `{}` (или метаданные карты).
- Плагины публикуют произвольные события через `ARpc_Core_Emit(event, payloadJson)`.
- Нотификации дропаются первыми при переполнении очереди (ответы — никогда).

## 6. Расширение методов Pawn-плагинами

Каталог не фиксирован: плагины регистрируют методы (нативы инклюда `AmxxRpc`, см.
[`include/AmxxRpc.inc`](../include/AmxxRpc.inc)). Мосту следует опираться на `rpc.methods`
(и `notifications/tools/list_changed`-аналог при необходимости — на стороне `amxb`).

## 7. Пример (Python, сырой TCP)

```python
import socket, json
s = socket.create_connection(("127.0.0.1", 27016), timeout=5); s.settimeout(5)
def call(method, params=None, i=1):
    s.sendall((json.dumps({"jsonrpc":"2.0","id":i,"method":method,"params":params},
                          separators=(",",":"))+"\n").encode())
    buf=b""
    while not buf.endswith(b"\n"): buf += s.recv(4096)
    return json.loads(buf)
print(call("rpc.auth", {"token": "<TOKEN>"}))
print(call("rpc.ping", None, 2))
print(call("fake.create", {"name": "Bot1", "authid": "STEAM_1:0:1"}, 3))
```

## 8. Совместимость

- Контракт версионируется `ARP_PROTO_VERSION` (`rpc.version`). Обратно-совместимые дополнения —
  новые методы/поля; удаление/переименование — только с повышением версии.
- Диагностика: серверные команды `amxxrpc_status`/`amxxrpc_clients` (не по RPC).
