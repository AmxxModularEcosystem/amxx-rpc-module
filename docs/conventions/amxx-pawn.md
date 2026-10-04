# Конвенции публичного Pawn-API AmxxRpc

> Переиспользуемые конвенции AMXX Pawn. Базовая база — общая (теги, `const`-параметры,
> `playerIndex`, явный `bool:`); здесь — специфика продукта AmxxRpc.

## 1. Префиксы

- Публичные нативы — `ARpc_<Owner>_*` (`ARpc_Core_RegisterMethod`, `ARpc_Core_Reply`).
- Обработчики — `@ARpc_<Owner>_<Name>` (дефолтный стиль регистрации, `public` не писать).
- Компиляционное время константы — `ARP_*` (`ARP_VERSION`, `ARP_ERR_*`).
- Теги — `T_ARpc_<Name>` + `Invalid_ARpc_<Name>` (первый член `enum`).
- Команды — `amxxrpc_*`.

## 2. Нативы

- Регистрация — **дефолтный стиль 0**:
  ```pawn
  register_native("ARpc_Core_RegisterMethod", "@ARpc_Core_RegisterMethod");
  ```
- Обработчик — `@` + полное имя; `pluginId` — неявный первый параметр.

## 3. Публичный инклюд

- Одна папка `include/AmxxRpc/`, основной — `Core.inc`; подключение `#include <AmxxRpc/Core>`.
- Только объявления и `stock`; версия — `#define ARP_VERSION "x.y.z"`.

## 4. Обработчики методов (продуктовая конвенция)

- Регистрация метода:
  ```pawn
  public plugin_precache()   // наш boot (см. ниже)
  {
      ARpc_Core_RegisterMethod("mydomain.action", "MyAction_Handler", "описание");
  }
  public MyAction_Handler(const requestId, const paramsJson[])
  {
      ARpc_Core_Reply(requestId, "{\"ok\":true}");
  }
  ```
- Точки входа — только `plugin_natives()` (регистрация нативов) и `plugin_precache()` (boot);
  `plugin_init`/`plugin_cfg` как наш порядок запуска не используем.
- Строки-`const`, escape — `^` (не `\`). Ошибка конфигурации — `abort(AMX_ERR_PARAMS, …)`.

## 5. Ответы

- Ровно один ответ на запрос: `ARpc_Core_Reply` **или** `ARpc_Core_ReplyError`.
- `resultJson` должен быть валидным JSON (модуль проверяет; невалидный → `-32603`).
- Асинхронный ответ разрешён, но только с главного потока; после таймаута ответ отбрасывается.
