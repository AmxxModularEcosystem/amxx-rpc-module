# 13. Фейк-игроки (дизайн)

Статус: **черновик (на согласование)**. Область: движковый слой создания/удаления фейк-игроков,
подмена authid, покадровый usercmd-драйв, RPC `fake.*`, ограничения. Требования — `docs/04`,
`docs/01` (AR-014…016, FR-FAKE-*), `docs/design/11` (транспорт), `docs/design/12` (реестр/ядро).

## 1. Термины и доступ к API

- **Фейк** = движковый fake-client: `edict_t` + реальный клиент-слот, `FL_FAKECLIENT|FL_CLIENT`.
- Доступные API (все — main thread; `USE_METAMOD` уже включён, `meta_api.h` даёт `MDLL_*`):
  - создание: `g_engfuncs.pfnCreateFakeClient(name)`;
  - инициализация: `MDLL_ClientConnect(ent, name, ip, reject)`, `MDLL_ClientPutInServer(ent)`;
  - движение: `g_engfuncs.pfnRunPlayerMove(ent, viewangles, fwd, side, up, buttons, impulse, msec)`;
  - authid-хук: `FN_GetPlayerAuthId` (moduleconfig.h);
  - удаление: `MDLL_ClientDisconnect(ent)` + удаление эдикта.
- `MDLL_*` — макросы метамода (`MDLL_FUNC->pfn…`), доступны из модуля.

## 2. Модель `FakeRecord`

```
struct FakeRecord {
  int            entIndex;      // 1..MAX_PLAYERS
  edict_t*       ent;
  std::string    authid;        // подменённый; буфер должен жить
  std::string    name;
  bool           alive;         // заспавнен
  float          viewAngles[3];
  float          fwd, side, up; // usercmd
  unsigned short buttons;       // зажатые (incl. IN_ATTACK)
  byte           impulse;
  bool           pendingRemove;
};
```
Реестр `entIndex → FakeRecord` на main-потоке. Внешний id фейка — `entIndex` (как индекс игрока),
но **только зарегистрированные фейки** принимаются методами `fake.*`.

## 3. Создание (FR-FAKE-001) — ревизия после ревью

**Критично (порядок по исходникам ReHLDS/AMXX):**
- `pfnCreateFakeClient` (движок) **сам** ставит `FL_FAKECLIENT|FL_CLIENT`, userinfo и вызывает
  `SV_ExtractFromUserinfo` → `gEntityInterface.pfnClientUserInfoChanged` (metamod-цепочка) →
  AMXX `C_ClientUserInfoChanged_Post`, где создаётся `CPlayer`, вызываются `Connect()/Authorize()`
  и читается **`GETPLAYERAUTHID`**. То есть регистрация в AMXX происходит **внутри** `pfnCreateFakeClient`.
- Значит authid должен быть доступен **до** вызова `pfnCreateFakeClient`. Решение — модульный
  `g_pendingAuthid`: ставим перед вызовом, снимаем после; хук `FN_GetPlayerAuthId` при
  `FL_FAKECLIENT` без записи отдаёт `g_pendingAuthid`.
- `MDLL_ClientConnect/PutInServer/ClientUserInfoChanged` **обходят** метамод-хуки (meta_api.h:
  «talk _directly_ to the gamedll, not multiplexed through Metamod»). Поэтому они нужны для
  создания `CBasePlayer` game-DLL, но **не** для регистрации в AMXX (её делает движок в шаге 3).

Последовательность:

1. Проверки: `fake_max`; имя непустое.
2. `g_pendingAuthid = authid`.
3. `ent = g_engfuncs.pfnCreateFakeClient(name)` — внутри движок: флаги, userinfo, `ClientUserInfoChanged`
   (**AMXX регистрирует бота и читает наш authid через `FN_GetPlayerAuthId`**). `nullptr` → `-32003`.
4. `entIndex`; снять `g_pendingAuthid`; создать `FakeRecord{entIndex, authid, …}` и внести в реестр.
5. `MDLL_ClientConnect(ent, name, "127.0.0.1", reject)`; reject → откат.
6. `MDLL_ClientPutInServer(ent)` (создаёт `CBasePlayer` через `GetClassPtr`).
7. Спавн при необходимости.
8. Ответ `{index, authid}`.

> Порядок 2→3 суть фикс: движок читает authid во время `pfnCreateFakeClient`, до возврата edict,
> поэтому запись «после create» (прежняя ревизия) — поздняя. Явный `MDLL_ClientUserInfoChanged` не нужен
> (и обошёл бы AMXX). Удаление через `MDLL_ClientDisconnect` тоже обходит AMXX (она слушает
> `SV_DropClient` через ReHLDS) — риск stale `CPlayer`; решение — см. §10a M2.

## 4. Подмена authid (FR-FAKE-007, AR-015)

- Движок отдаёт всем фейкам `"BOT"` (`PF_GetPlayerAuthId`). Хук `FN_GetPlayerAuthId(edict_t*)`:
  если эдикт — зарегистрированный фейк → вернуть его `authid` (`RETURN_META_VALUE(MRES_SUPERCEDE, buf)`),
  иначе — обычное поведение.
- Буфер строки должен жить постоянно (per-fake storage).
- Хук обязан надёжно различать фейка и реального игрока (по реестру); после удаления — запись снимается.

## 5. Покадровый usercmd-драйв (FR-FAKE-008)

- В `StartFrame` (main) для каждого `alive`-фейка:
  `pfnRunPlayerMove(ent, viewAngles, fwd, side, up, buttons, impulse, msec)`, где `msec = frametime`
  (не throttle). Guard: только alive/spawned, иначе — «призрак»/краш.
- Без покадрового вызова фейк без bbox (см. `docs/04`).

## 6. RPC-методы `fake.*`

| Метод | Назначение |
|---|---|
| `fake.create` | `{name, authid?, team?}` → `{index, authid}` |
| `fake.remove` | `{index}` → `{ok:true}` |
| `fake.list` / `fake.get` | состояние фейков |
| `fake.move` | `{index, forward, side, up}` |
| `fake.look` | `{index, angles:[x,y,z]}` или `{index, at:[x,y,z]}` |
| `fake.stop` | сброс движения/кнопок |
| `fake.buttons` | `{index, press:[...], release:[...]}` (incl. `IN_ATTACK`) |
| `fake.set` | `{index, health?, armor?, team?, weapon?}` |
| `fake.authid` | `{index, authid?}` — задать/прочитать |

- Все методы — **main thread**; `fake.*` **отвергает индексы реальных игроков** (`FR-FAKE-009`).

## 7. Удаление (FR-FAKE-010)

- `pendingRemove`-флаг; фактическое удаление — на main в think:
  `MDLL_ClientDisconnect(ent)` + удаление эдикта + освобождение записи + снятие authid-буфера.
- Без «висячих» эдиктов и утечки слотов.

## 8. Ограничения/защита

- `fake_max` (конфиг) — не исчерпывать клиент-слоты (`FR-FAKE-009`).
- Индексы реальных игроков отвергаются.
- Смерть/респавн фейка/смена карты — запись остаётся, но `alive` пересчитывается; на `map_start`
  фейки могут быть сброшены (решить: пересоздавать или удалять — в плане).

## 9. Взаимодействие с YAPB

- Этап 3 — **свой** слой (болванчики). YAPB (навигация) — Этап 4, опционально (`design/14`).

## 10. Риски

- Движковые вызовы (`CreateFakeClient`/`ClientConnect`/`RunPlayerMove`) HLD S/ReHLDS-специфичны;
  рантайм-проверка — на тест-сервере.
- Регистрация в AMXX — через `ClientUserInfoChanged` (§3), а не `ClientConnect`; порядок 3/4/9 критичен.
- `MUTIL_CallGameEntity(PLID, "player", …)` (фабрика игрока) — нужность/доступность уточняется в реализации.
- Покадровый драйв обязателен; guard от dead/not-spawned; удаление — корректное освобождение слота.

## 10a. Ревизия после ревью (резолвы)

| # | Проблема ревью | Решение |
|---|---|---|
| B1 | Регистрация бота AMXX — в `C_ClientUserInfoChanged_Post`, не `ClientConnect` | §3 переписан: `FakeRecord`+authid+`FL_FAKECLIENT` до `ClientUserInfoChanged`; явный вызов `MDLL_ClientUserInfoChanged`. |
| B2 | `MDLL_ClientConnect/PutInServer` **обходят** хуки метамода | Осознано: на регистрацию в AMXX не рассчитываем; регистрация — через userinfo-changed. |
| B3 | Пропущена фабрика игровой сущности (`player()`) | Добавлен шаг `MUTIL_CallGameEntity(PLID,"player",&ent->v)` (или `GetClassPtr` через PutInServer). |
| B4 | `FN_GetPlayerAuthId` — Metamod pre-hook с `RETURN_META_VALUE(MRES_SUPERCEDE, buf)` | Подтверждено: корректный pre-hook; постоянный буфер; снятие при remove. |
| M1 | Порядок «реестр после spawn» конфликтовал с authid-чтением | Реестр/запись — до `ClientUserInfoChanged` (§3). |
| M2 | Удаление не детализировано; `MDLL_ClientDisconnect` обходит AMXX | **Блокер:** AMXX слушает `SV_DropClient` (ReHLDS hookchain) — удаление фейка через `MDLL_ClientDisconnect` не уведомит AMXX (stale `CPlayer`). Варианты: (а) дропать через движковый `SV_DropClient` (нужен ReHLDS-hookchain/API-вызов); (б) принять stale до mapchange/чистить; (в) hook `SV_DropClient` для фейков. Решение — в реализации с рантайм-проверкой на сервере. Плюс `MDLL_ClientDisconnect` + удаление эдикта + снятие записи/буфера. |
| M3 | `StartFrame` при пустом сервере (B-4 Этапа 1) | Предпосылка для `Fake_Think`; подтверждается владельцем; при отсутствии тика — pump-абстракция (Этап 1). |
| M4 | Реальные игроки | `fake.*` отвергает незарегистрированные индексы; `fake_max`. |
| M5 | Смерть/респавн/смена карты | `alive` пересчитывается; на `map_start` — пере-регистрация `FL_FAKECLIENT`/userinfo (решить в реализации). |

## 11. Верификация

- Сборка win/linux 0 ошибок; пример `amxxpc` 0/0 (регрессия).
- На тест-сервере (владелец): `fake.create` (слот/флаги/уникальный authid), `fake.move/look/buttons`
  (движение/выстрел), `fake.set`, `fake.remove` (слот освобождён), отказ на реальном игроке.
