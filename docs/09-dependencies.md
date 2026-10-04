# 09. Зависимости

## 1. Compile-time (вендорятся в репозиторий)

| Зависимость | Где | Назначение |
|---|---|---|
| AMXX module SDK | `sdk/` (копия `public/sdk/amxxmodule.{h,cpp}`, `moduleconfig.in.h`) | ABI модуля; **не редактируется** |
| `public/amxmodx_version.h` | `sdk/` | `AMXX_VERSION` для `MODULE_VERSION` |
| `public/IGameConfigs.h`, `public/ITextParsers.h`, AMTL | `sdk/` | транзитивные include из `amxxmodule.h` |
| Metamod HL1 headers | `sdk/metamod/` | `meta_api.h`, `osdep.h`, `engine_t.h`, `dllapi.h`, … (из-за `USE_METAMOD`) |
| HLSDK headers | `sdk/hlsdk/` | `common/`, `dlls/`, `engine/`, `game_shared/`, `public/` |
| JSON-библиотека | `third_party/` | парсинг/сериализация JSON-RPC; кандидат — `parson` (MIT) |

> Точный список/версии фиксируются в дизайне сборки. SDK вендорится копией (наиболее
> воспроизводимо) — либо submodule/FetchContent (OQ-9).

## 2. Runtime

| Зависимость | Обязательность | Примечание |
|---|---|---|
| Metamod (GoldSrc) `1.21+` | **обязательна** | хост для AMXX-модулей; движковые хуки |
| AMX Mod X `1.10.5428` | **обязательна** | целевая версия тест-сервера |
| HLDS/ReHLDS (CS 1.6) | **обязательна** | однопоточный процесс |
| YAPB | **опциональна** | только навигация ботов; dlopen `GetBotAPI`; отсутствие → degrade |

Политика: по умолчанию — версии из целевого окружения (`1.10.5428`); опциональные
зависимости не должны становиться обязательными (`NFR-PORT-002`, `AR-016`).

## 3. Инструменты сборки/CI

| Инструмент | Назначение |
|---|---|
| CMake ≥ 3.20 | сборка win/linux |
| GCC + multilib (`gcc-multilib`, `g++-multilib`, `libc6-dev-i386`) | Linux `-m32` |
| MSVC (Win32) | Windows `_amxx.dll` |
| `amxxpc 1.10.5428` | компиляция инклюда/примера (`0/0`) |

## 4. Открытые вопросы

- Выбор JSON-библиотеки (OQ-4).
- Стратегия вендоринга SDK (OQ-9).
- Минимальная версия Metamod/ReHLDS (уточняется на тест-сервере).
