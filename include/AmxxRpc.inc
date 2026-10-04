/**
 * AmxxRpc — публичный Pawn API.
 *
 * Транспортный C++-модуль AMX Mod X (Metamod) открывает внешним инструментам
 * JSON-RPC 2.0 транспорт (TCP loopback + токен). Плагины публикуют собственные
 * RPC-методы через нативы этого инклюда и отвечают на входящие запросы.
 *
 * Подключение:
 *     #include <AmxxRpc>
 *
 * Регистрация метода (обычно в plugin_precache):
 *
 *     public plugin_precache()
 *     {
 *         ARpc_Core_RegisterMethod("mydomain.action", "MyAction_Handler", "Do something");
 *     }
 *
 *     public MyAction_Handler(const requestId, const paramsJson[])
 *     {
 *         ARpc_Core_Reply(requestId, "{\"ok\":true}");
 *     }
 *
 * @note Метод должен быть зарегистрирован до первого вызова снаружи.
 * @note Ровно один ответ на запрос: ARpc_Core_Reply ИЛИ ARpc_Core_ReplyError.
 */

#if defined _amxxrpc_included
	#endinput
#endif
#define _amxxrpc_included

/**
 * Версия модуля/инклюда (compile-time).
 *
 * Пример: `public stock const PluginVersion[] = ARP_VERSION;`
 */
#define ARP_VERSION "0.1.3"

/** Код ошибки: некорректные параметры запроса. */
#define ARP_ERR_INVALID_PARAMS      -32602
/** Код ошибки: внутренняя ошибка обработчика/модуля. */
#define ARP_ERR_INTERNAL            -32603
/** Код ошибки: сервис недоступен (например, YAPB не установлен). */
#define ARP_ERR_SERVICE_UNAVAILABLE -32002
/** Код ошибки: ошибка исполнения на стороне движка/AMXX. */
#define ARP_ERR_ENGINE              -32003

/**
 * Регистрирует RPC-метод, обрабатываемый публичной функцией плагина.
 *
 * @param name          Имя метода в формате `<область>.<действие>` (например, `demo.echo`).
 * @param callback      Имя публичной функции плагина-обработчика с сигнатурой
 *                      `callback(const requestId, const paramsJson[])`.
 * @param description   Необязательное человекочитаемое описание (для каталога `rpc.methods`).
 *
 * @return              `true` при успехе; `false` если аргументы некорректны, имя занято,
 *                      предпринята попытка переопределить встроенный/транспортный метод,
 *                      либо публичная функция `callback` не найдена.
 *
 * @note Обработчик вызывается на главном потоке сервера.
 * @note Один плагин не может переопределить методы ядра (`rpc.*`, `server.*`, `cvar.*`)
 *       или методы другого плагина.
 */
native bool:ARpc_Core_RegisterMethod(const name[], const callback[], const description[] = "");

/**
 * Снимает регистрацию метода, ранее добавленного этим плагином.
 *
 * @param name          Имя метода.
 *
 * @return              `true`, если метод снят; `false`, если метод не найден или
 *                      принадлежит другому плагину.
 */
native bool:ARpc_Core_UnregisterMethod(const name[]);

/**
 * Отправляет успешный ответ на запрос, диспатчнутый этому плагину.
 *
 * @param requestId     Идентификатор запроса, переданный в обработчик.
 * @param resultJson    Результат в виде JSON-строки (валидируется модулем).
 *
 * @return              `true`, если ответ принят; `false`, если `resultJson` не является
 *                      валидным JSON (тогда запрос завершается ошибкой `ARP_ERR_INTERNAL`),
 *                      либо запрос истёк/принадлежит другому плагину.
 *
 * @note Ровно один ответ на запрос. Повторный ответ игнорируется.
 * @note Ответ после таймаута запроса отбрасывается.
 */
native bool:ARpc_Core_Reply(const requestId, const resultJson[]);

/**
 * Отправляет ответ-ошибку на запрос, диспатчнутый этому плагину.
 *
 * @param requestId     Идентификатор запроса, переданный в обработчик.
 * @param errorCode     Код ошибки JSON-RPC (см. `ARP_ERR_*`).
 * @param message       Текст ошибки.
 *
 * @return              `true`, если ответ принят; `false`, если запрос истёк/принадлежит
 *                      другому плагину.
 *
 * @note Ровно один ответ на запрос.
 */
native ARpc_Core_ReplyError(const requestId, const errorCode, const message[]);

/**
 * Публикует именованное событие всем подписанным внешним сессиям (push-нотификация).
 *
 * @param event         Имя события (например, `myplugin.something`).
 * @param payloadJson   Полезная нагрузка в виде JSON-строки (валидируется модулем).
 *
 * @return              `true`, если событие отправлено; `false` при некорректных аргументах
 *                      или невалидном JSON.
 */
native ARpc_Core_Emit(const event[], const payloadJson[]);

/**
 * Проверяет, есть ли хотя бы одна аутентифицированная внешняя сессия.
 *
 * @return              `true`, если подключён хотя бы один аутентифицированный клиент.
 */
native ARpc_Core_IsConnected();

/**
 * Записывает версию модуля в буфер.
 *
 * @param out           Буфер для строки версии.
 * @param len           Размер буфера.
 *
 * @return              Длина записанной строки.
 */
native ARpc_Core_GetVersion(out[], len);
