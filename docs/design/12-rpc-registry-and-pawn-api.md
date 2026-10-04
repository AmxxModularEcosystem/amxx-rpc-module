# 12. Реестр методов и Pawn-API (дизайн)

Статус: **черновик (на согласование)**. Область: реестр RPC-методов, диспетчер, формат
`requestId`, async-ответы, нативы `ARpc_Core_*`, встроенное ядро методов, подписки/события,
обработка выгрузки плагина. Требования — `docs/03`, `docs/01` (AR-011…013, FR-RPC-*),
`docs/design/11` (транспорт), `docs/design/10` §4.

## 1. Реестр

- Запись: `{ name, source: builtin|pawn, description, pluginId (для pawn), forwardId }`.
- Имена — `<область>.<действие>`.
- **Коллизии:** повторная регистрация Pawn-именем — ошибка (натив возвращает `false`);
  **core-методы плагину переопределять запрещено** (`design/10` §7.10).
- `rpc.methods` возвращает `[{name, source, description}]` (минимальные метаданные для MCP-моста).

## 2. Диспетчеризация (главный поток)

- Диспетчер вызывается из `Rpc_Tick()` (`StartFrame`): pop из `inbox` до `drain_budget`.
- `builtin` → C++-обработчик (движок/AMXX можно — главный поток).
- `pawn` → вызов SP-forward метода (см. §4).
- Ответ → `outbox` (per-session), `isNotification` по запросу.
- **`rpc.auth`** — транспортный, в реестре помечен `source=transport` (не вызывается как метод).

## 3. `requestId` (OQ-3)

- Внутренний **handle**: **монотонный `uint32`** (как в Этапе 1: `g_nextHandle++`), `0` — sentinel
  (notification/невалидный), никогда не выдаётся. **Без generation/slot** — «не переиспользуется»
  (согласовано с `design/11` §6/§14; generation/slot не нужен и противоречил бы «never reused»).
- Таблица outstanding на главном потоке: `handle → { sessionId, rawId, deadline, methodName, pluginId }`
  (Этап 1 расширяется полями `methodName`/`pluginId`).
- **Async-ответ разрешён** (Pawn может ответить позже), но только с главного потока.
- **Таймаут** `request_timeout` → `-32005`, запись снимается; поздний/повторный `Reply` — игнорируется.
- Notification (`id=null`) → handle не выдаётся (sentinel `0`); `Reply(0, …)` — игнор.
- Handle передаётся в Pawn как `cell`; при `> 2^31` может быть отрицательным — натив кастует обратно в `uint32`.

## 4. Вызов Pawn-обработчика (OQ-2)

- Решение: **per-method SP-forward**. При `ARpc_Core_RegisterMethod(name, callback)` модуль создаёт
  форвард по имени `callback`:
  `MF_RegisterSPForwardByName(MF_GetScriptAmx(pluginId), callback, FP_CELL, FP_STRING, FP_DONE)`.
  **Важно:** первый аргумент — `AMX*` (не pluginId); берётся из `MF_GetScriptAmx(pluginId)`.
  Если функция не найдена (возврат `-1`) → `RegisterMethod` возвращает `false`.
- **Callback — публичная функция плагина с произвольным именем** (не `@ARpc_*`; `@ARpc_*` — это
  обработчики нативов модуля, другая сущность). Уточнить формулировку в `docs/conventions/amxx-pawn.md`.
- Вызов: `MF_ExecuteForward(forwardId, handle, paramsJson)` (регистрация `FP_CELL, FP_STRING, FP_DONE`).
- **Проверка живости:** перед вызовом убедиться, что владеющий плагин ещё загружен (по `pluginId`/`AMX`;
  при неуверенности — метод снимается). `MF_ExecuteForward` возвращает `-1` только для невалидного id;
  для «мёртвого, но валидного» форварда может вернуть `0` (неотличимо от `PLUGIN_CONTINUE`) — поэтому
  **не полагаться только на код возврата** (см. §8).
- `paramsJson` — JSON-строка params (или `"null"`); `method`/`id` не передаются.

## 5. Pawn-API (`include/AmxxRpc.inc`)

```pawn
native bool:ARpc_Core_RegisterMethod(const name[], const callback[], const description[] = "");
native bool:ARpc_Core_UnregisterMethod(const name[]);
native ARpc_Core_Reply(const requestId, const resultJson[]);
native ARpc_Core_ReplyError(const requestId, const errorCode, const message[]);
native ARpc_Core_Emit(const event[], const payloadJson[]);
native ARpc_Core_IsConnected();
native ARpc_Core_GetVersion(out[], len);
```
- Стиль — дефолтная регистрация (`ARpc_Core_*`, `@ARpc_Core_*`), см. `docs/conventions/amxx-pawn.md`.
- `resultJson` валидируется модулем (parson); невалидный → `-32603` (запрос завершается ошибкой).
- **Ownership-check (безопасность):** `ARpc_Core_Reply`/`ReplyError` принимает ответ только если
  `handle` принадлежит запросу, диспатчнутому **этому же** `pluginId`; иначе игнор/ошибка.
  `ARpc_Core_UnregisterMethod` снимает только методы своего `pluginId`. (Иначе плагин может подменить
  чужой ответ или снять чужой метод.)
- `ARpc_Core_Emit`: `payloadJson` также валидируется; событие уходит подписчикам.
- Набор `ARP_ERR_*`: `-32602` (invalid params), `-32603` (internal), `-32002` (service unavailable),
  `-32003` (engine). (`-32001` — транспортный, плагину недоступен.)

## 6. Встроенное ядро методов

| Метод | Назначение |
|---|---|
| `server.exec` | исполнить серверную команду; возврат `{ok:true}`. **OQ-13 закрыт: захват вывода не делаем в v1**; требуется обновить `FR-RPC-008` (см. §12). |
| `cvar.get` / `cvar.set` | чтение/запись cvar (`{name,value}`). |
| `players.list` / `players.get` | игроки: индекс, имя, authid, команда, health, origin. |
| `events.subscribe` / `events.unsubscribe` | подписка сессии на именованные события. |
| `rpc.methods` | каталог методов: `[{name, source, description}]`, включая `rpc.auth` с `source="transport"`. (Формат Этапа 1 заменяется на этот.) |

- `rpc.ping`/`rpc.version` (Этап 1) **сохраняются** в ядре.
- `logs.tail` — **отложен**; раундовые/килл-события — **отложены**. Требуется обновить `FR-RPC-005`
  (см. §12).

## 7. События (push)

- Подписки — на именованные события; `outbox` с `isNotification=true`.
- Карта подписок принадлежит **главному потоку** (и subscribe, и emit — main): блокировки не нужны.
- **Lifecycle:** main не получает уведомления о закрытии сессии (её ведёт I/O-поток). Мёртвые подписки
  прунятся **лениво**: при push в `outbox` для отсутствующей сессии запись удаляется; плюс периодическая
  чистка по факту «сессии нет». `ARpc_Core_IsConnected` требует счётчика **аутентифицированных** сессий
  (добавить `Transport_AuthenticatedCount()`; `Transport_ClientCount` считает все).
- v1-источники (через metamod-хуки, главный поток): `player_connect`, `player_disconnect`,
  `map_start` (ServerActivate). Хуки `FN_ClientConnect`/`FN_ClientDisconnect`/`FN_ServerActivate`
  добавить в `moduleconfig.h`. Плюс произвольные от плагинов (`ARpc_Core_Emit`).
- Раундовые/килл-события — **отложены** (нужен AMXX/game event источник).
- Формат нотификации: `{"jsonrpc":"2.0","method":<event>,"params":<payload>}`.

## 8. Снятие при выгрузке плагина (OQ-14)

- На `FN_AMXX_PLUGINSUNLOADING` (**глобальная** выгрузка всех плагинов, напр. смена карты) — снять
  **все** Pawn-методы: `MF_UnregisterSPForward(forwardId)` для каждого + завершить их pending ошибкой.
- `ARpc_Core_UnregisterMethod` (только свой `pluginId`) — `MF_UnregisterSPForward` + удаление записи.
- **Ограничение (важно):** в AMXX `1.10` **нет per-plugin unload-хука** для модулей, и AMXX **не**
  авто-снимает SP-форварды при выгрузке отдельного плагина (`CPlugin::~CPlugin` не трогает форварды;
  `CSPForward::execute` разыменовывает `m_Amx`). Поэтому вызов форварда снятого плагина —
  **UAF/чужой плагин**. Митигация v1: (а) не хранить форварды дольше глобальной выгрузки;
  (б) перед вызовом проверять, что владелец (`pluginId`/`AMX`) жив; (в) не полагаться на код возврата
  `MF_ExecuteForward`; (г) **документировать**, что индивидуальная выгрузка плагина в v1 не
  поддерживается надёжно (FR-RPC-009 — с оговоркой, см. §12).

## 9. Пример-плагин (OQ-10)

- В обязательной поставке — `examples/AmxxRpcExample.sma`, регистрирующий демо-метод
  (напр. `demo.echo`), отвечающий через `ARpc_Core_Reply`. Проверяется `amxxpc` 0/0.

## 10. Верификация

- Сборка 0 ошибок; off-line unit-тест реестра (`tests/registry_test`, без AMXX) — коллизии,
  core-override запрет, метаданные `rpc.methods`.
- На тест-сервере (владелец): плагин регистрирует метод → вызов по RPC → ответ; async-ответ;
  таймаут; подписка на `player_connect` → push; `server.exec`/`cvar.set`/`players.list`.
- `amxxpc`: инклюд + пример — 0/0.

## 11. Открытые вопросы

- OQ-2/3/13/14 закрываются здесь. OQ-10 закрыт (пример в поставке).
- Источник раундовых/килл-событий — отдельный этап (за пределами v1 ядра).

## 12. Требования к обновлению и резолвы ревью

Обновить требования под решения Этапа 2 (согласование владельцем — переход к реализации):

- **FR-RPC-008** — `server.exec` возвращает `{ok:true}` (захват вывода не делаем в v1) либо
  переформулировать; иначе конфликт с дизайном.
- **FR-RPC-005** — `logs.tail` и раундовые/килл-события **отложены**; v1-события — connect/disconnect/map.
- **FR-RPC-009** — снять методов при выгрузке: глобально (`PLUGINSUNLOADING`) — поддерживается; для
  **индивидуальной** выгрузки плагина — оговорка (нет per-plugin хука; см. §8).

Резолвы ревью (свёрнуты выше):

- **B1** `MF_RegisterSPForwardByName(AMX*, …)` через `MF_GetScriptAmx(pluginId)`.
- **B2** UAF/чужой-плагин при вызове форварда снятого плагина — митигации §8; не полагаться на код возврата.
- **B3** handle — монотонный `uint32` Этапа 1 (без generation/slot).
- **M1** ownership-check в `Reply`/`Unregister` (§5).
- **M2** валидация `Reply`/`Emit` JSON (§5).
- **M3** формат `rpc.methods` — `[{name,source,description}]` (§6).
- **M4** подписки: владение main, ленивая прунинг, authenticated-count (§7).
- **M5** `moduleconfig.h` + `FN_ClientConnect/Disconnect/ServerActivate`, `FN_AMXX_PLUGINSUNLOADING`.
- **M6** унифицировать формулировку callback (публичная функция плагина, не `@ARpc_*`) в конвенциях.
- **M7** off-line unit-тест реестра — отделить логику реестра от AMXX-вызовов (интерфейс forward-регистратора).
- **M8** рефактор Этапа 1: `Reply(handle, …)` ищет `sessionId` в outstanding; расширить `Outstanding`
  (`methodName`/`pluginId`); `DispatchMethod` → реестр.
