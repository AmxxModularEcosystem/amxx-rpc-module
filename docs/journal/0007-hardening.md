# 0007. Этап 5 — Hardening (упаковка, e2e, доки)

**Дата:** 2026-10-04 · **Этап:** 5 Hardening · **Статус:** реализовано; e2e на реальном сервере зелёный

## Контекст

Завершающий этап: без новой функциональности — упаковка артефактов, сводный e2e-раннер,
финализация README/доков, актуализация канонических команд. План — `.omo/plans/0007`.

## Сделано

- **CMake install/package**: `install(TARGETS amxxrpc … modules)` + `install(DIRECTORY include/AmxxRpc …)`
  + `install(FILES configs/amxxrpc.cfg …)` → раскладка `addons/amxmodx/{modules,scripting/include/AmxxRpc,configs}`.
- **`scripts/e2e_all.py`**: сводный e2e (транспорт/кодек + фейки + деградация ботов), exit-код.
- **README**: финальный — возможности, сборка (MSVC/CMake + zig+`.def`), install, установка, инклюд, проверка, карта доков.
- **Docs sync**: `docs/06` (канонические команды: Linux g++ эквивалент, Windows zig+`.def`, e2e),
  `docs/09` (parson, YAPB), `docs/05` (`yapb_self_load`).

## Верификация (реальный HLDS-сервер, Windows)

- Сборка win/linux — 0 ошибок; `src/` warnings 0.
- Off-line: `protocol_test` 54/0, `registry_test` 60/0, `bot_test` 62/0.
- **`scripts/e2e_all.py` — 0 failures**: transport/codec, `fake.create/get/move/remove` + отказ
  реальному игроку, `bot.available=false`, `bot.add -32002`.
- Регрессия: `smoke_tcp.py` 12/0, `e2e_fake.py` lifecycle OK.

## Открытые нити (не блокирующие)

- Нативная MSVC-сборка/CI-прогон (нет окружения) — владельцу.
- Наличие YAPB на сервере для проверки presence-пути (ABI R-3) — владельцу.
- `authid`-override подтверждён lifecycle-ом, но не независимым Pawn-наблюдением.
- Переименование папки `amxb-server-mcp → amxx-rpc` — после закрытия сессии (ОС-лок).

## Затронутые артефакты

- `CMakeLists.txt`, `scripts/e2e_all.py`, `README.md`, `docs/05`, `docs/06`, `docs/09`,
  `docs/journal/0007-hardening.md`, индекс журнала, `.omo/plans/0007`.

## Итог проекта

Все этапы дорожной карты (0–5) реализованы и закоммичены; транспорт, реестр/Pawn-API, фейк-игроки
и адаптер YAPB — с проверкой на реальном HLDS (кроме presence-пути YAPB). Модуль — чистый транспорт;
интеграция MCP — в `amxb` (вне репозитория).
