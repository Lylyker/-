/* ====================================================================
 * logger.c — модуль файлового логирования событий.
 * Каждая запись содержит временную метку, уровень и сообщение.
 * Логгер потокобезопасен: используется собственный мьютекс, поэтому
 * писать в app.log могут одновременно несколько потоков.
 * ==================================================================== */
#include "app.h"
#include <stdarg.h>

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

void log_event(const char *level, const char *fmt, ...)
{
    /* 1. Формируем текст сообщения в локальном буфере (ва-аргументы) */
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    /* 2. Получаем человекочитаемую временную метку */
    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_buf);

    /* 3. Критическая секция: открываем файл в режиме дозаписи,
     *    пишем строку и закрываем. Мьютекс исключает перемешивание строк. */
    pthread_mutex_lock(&log_mutex);
    FILE *f = fopen(LOG_FILE, "a");
    if (f) {
        fprintf(f, "[%s] [%-5s] %s\n", ts, level, msg);
        fclose(f);
    }
    pthread_mutex_unlock(&log_mutex);

    /* 4. Дублируем важные события в консоль для наглядности демонстрации */
    printf("%s\n", msg);
}
