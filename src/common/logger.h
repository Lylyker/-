/* logger.h — модуль логирования событий (общий для обеих программ) */
#ifndef LOGGER_H
#define LOGGER_H

#include <stddef.h>

typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} log_level_t;

/* Инициализация логгера: открытие файла логов (режим добавления - append). */
int logger_init(const char *path);

/* Запись сообщения с указанием уровня и имени потока. Потокобезопасно. */
void logger_write(log_level_t level, const char *thread_name, const char *fmt, ...);

/* Закрытие логгера. */
void logger_close(void);

#define LOG_D(tname, ...) logger_write(LOG_DEBUG, tname, __VA_ARGS__)
#define LOG_I(tname, ...) logger_write(LOG_INFO,  tname, __VA_ARGS__)
#define LOG_W(tname, ...) logger_write(LOG_WARN,  tname, __VA_ARGS__)
#define LOG_E(tname, ...) logger_write(LOG_ERROR, tname, __VA_ARGS__)

#endif /* LOGGER_H */
