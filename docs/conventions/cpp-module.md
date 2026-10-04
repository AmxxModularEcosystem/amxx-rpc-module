# Конвенции C++-модуля AmxxRpc

## 1. Границы и SDK

- `src/` — наш код. `sdk/` (`public/sdk/amxxmodule.{h,cpp}`, `amxxmodule_version`,
  `IGameConfigs.h`, Metamod/HLSDK) — **вендорится и не редактируется**.
- Правится только `moduleconfig.h` (по шаблону `moduleconfig.in.h`): `MODULE_*`, `USE_METAMOD`,
  `FN_AMXX_*`.
- Публичные интерфейсы между подсистемами — узкие `struct`/функции в `src/`, не в `sdk/`.

## 2. Именование

- Функции-владельцы — `PascalCase` с префиксом подсистемы: `Transport_*`, `Protocol_*`,
  `Queue_*`, `Rpc_*`, `Core_*`, `Fake_*`, `Bot_*`, `Config_*`, `Log_*`.
- Файлы — `PascalCase.{h,cpp}` (`Transport.h/.cpp`).
- Типы/структуры — `PascalCase` (`FakeRecord`, `RpcRequest`).
- Приватные члены — `m_` / локальные — `camelCase`.

## 3. Потоки (blocking-правило)

- **I/O-поток:** только сокет/фрейминг/JSON; **никогда** AMXX, нативы, движок, игровое состояние.
- **Главный поток:** AMXX/движок/нативы/Pawn; дренаж очередей по `StartFrame`.
- Обмен — только через `Queue_*` (отдельный мьютекс на очередь; без вложенных захватов;
  condition variable). Логирование из I/O — через потокобезопасный сток (не `MF_Log*`).
- Shutdown: wakeup-канал + join; идемпотентно.

## 4. Ошибки и ресурсы

- Ошибки конфигурации — fail-fast (лог + не поднимать транспорт), без «тихой деградации».
- Не глушить ошибки пустыми `catch`/игнором; не терять код возврата.
- RAII для сокетов/файлов/буферов; никаких утечек слотов фейков.
- Секреты (токен) не логируются и не эхоятся.

## 5. Совместимость

- C++17. `HAVE_STDINT_H`. Windows (MSVC, `/MT`) + Linux (`-m32`).
- Кроссплатформенные примитивы — `#ifdef _WIN32` (Winsock/BSD; `select`/`WSAPoll`; `poll`
  на Windows отсутствует).
- Сборка — CMake; суффикс `_amxx`/`_amxx_i386`.
