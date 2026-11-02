/* logger.c — реализация потокобезопасного логгера */
#include "logger.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

static FILE *g_log_file = NULL;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char *level_name(log_level_t lvl)
{
    switch (lvl) {
    case LOG_DEBUG: return "DEBUG";
    case LOG_INFO:  return "INFO ";
    case LOG_WARN:  return "WARN ";
    case LOG_ERROR: return "ERROR";
    default:        return "?????";
    }
}

int logger_init(const char *path)
{
    pthread_mutex_lock(&g_log_mutex);
    if (g_log_file) {           /* уже открыт — закрываем старый */
        fclose(g_log_file);
        g_log_file = NULL;
    }
    g_log_file = fopen(path, "a");
    pthread_mutex_unlock(&g_log_mutex);
    return g_log_file ? 0 : -1;
}

void logger_write(log_level_t level, const char *thread_name, const char *fmt, ...)
{
    char buf[1024];
    char stamp[32];
    va_list ap;
    time_t t;
    struct tm tm_buf;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    t = time(NULL);
    localtime_r(&t, &tm_buf);
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm_buf);

    pthread_mutex_lock(&g_log_mutex);
    if (g_log_file) {
        fprintf(g_log_file, "[%s] [%s] [%s] %s\n",
                stamp, level_name(level),
                thread_name ? thread_name : "main", buf);
        fflush(g_log_file);
    }
    pthread_mutex_unlock(&g_log_mutex);
}

void logger_close(void)
{
    pthread_mutex_lock(&g_log_mutex);
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
    pthread_mutex_unlock(&g_log_mutex);
}
