# Work Plan: Этап 5 — Hardening (AmxxRpc)

## Profile bindings

- Дизайн-гейт: — (hardening: упаковка/доки/e2e; новой подсистемы нет).
- Ревью: right-size к риску (низкий) — формальный design-review не требуется; план-ревью `momus` опц.
- Планы: `.omo/plans/`. Журнал: `docs/journal/` (запись `0007`).
- Коммит — владелец разрешил коммитить каждый этап.

## Goal

Довести до поставки: упаковка артефактов, сводный e2e-раннер, финализация README/доков,
актуализация канонических команд верификации. Без новой функциональности.

## Deliverables

| Артефакт | Что |
|---|---|
| `CMakeLists.txt` | install/packaging: `addons/amxmodx/{modules,scripting/include/AmxxRpc,configs}` |
| `scripts/e2e_all.py` | сводный e2e (smoke + fake + bot), exit≠0 при провале |
| `README.md` | финал: сборка (MSVC/CMake + zig/`.def`), деплой, быстрый старт, карта методов |
| `docs/05` | финальные ключи (вкл. `yapb_self_load`); `amxxrpc_status` |
| `docs/06` | канонические команды (Linux g++ эквивалент, Windows zig+`.def`, e2e) |
| `docs/09` | YAPB (опц.), parson |
| `docs/journal/0007-hardening.md` | запись + индекс |

## Steps

1. **CMake install/package** — `install(TARGETS ...)` в `addons/amxmodx/modules`, `install(DIRECTORY include/AmxxRpc ...)`, `install(FILES configs/amxxrpc.cfg ...)`; опц. `package` (zip).
2. **scripts/e2e_all.py** — запускает smoke/fake/bot-сценарии, агрегирует, exit-код.
3. **README** — финальная структура, сборка, деплой, методы.
4. **Docs sync** — `docs/05`/`06`/`09`.
5. **Verify** — сборки win/linux 0 ошибок; все off-line тесты; e2e_all на сервере.
6. **Record** — журнал `0007` + индекс.

## Acceptance criteria (verifiable)

- Сборка win/linux 0 ошибок; `protocol`/`registry`/`bot` тесты зелёные.
- `scripts/e2e_all.py` на реальном сервере — все секции ok (smoke 12, fake lifecycle, bot degradation 15).
- `cmake --install` раскладывает `addons/amxmodx/...` (проверка структуры; локально cmake нет → эквивалент).
- README отражает реальные команды.

## Verification (точные команды)

```
# module (Linux) + tests + Windows (zig+.def) — как в планах 0005/0006
# e2e:
python scripts/e2e_all.py 127.0.0.1 27016 <token>
```

## MUST NOT

- Добавлять новую функциональность; обязательные зависимости; MCP.
- Коммитить без указания владельца (владелец разрешил).

## Out of scope

- Нативная MSVC-сборка/CI-прогон (нет окружения) — нить.
- Наличие YAPB на сервере — нить.
