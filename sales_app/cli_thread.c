/* ====================================================================
 * cli_thread.c — ПОТОК 5: интерактивный CLI-интерфейс.
 * Работает в бесконечном цикле: читает строки пользователя,
 * меняет глобальный VIP-порог и будит аналитические потоки через
 * pthread_cond_broadcast(&g_param_changed). Команда "exit" завершает
 * всё приложение (сбрасывает g_running).
 * ==================================================================== */
#include "app.h"
#include <ctype.h>

static void print_help(void)
{
    printf("\n------------------ СПРАВКА ---------------------------\n");
    printf("  <число>   — установить новый порог VIP-клиентов (в рублях),\n");
    printf("              например: 50000\n");
    printf("  threshold — показать текущий порог\n");
    printf("  help      — эта справка\n");
    printf("  exit      — завершить приложение (все потоки)\n");
    printf("------------------------------------------------------\n\n");
}

void *cli_thread(void *arg)
{
    (void)arg;
    log_event("INFO", "[Поток CLI] запущен");
    print_help();

    char line[128];
    while (g_running) {
        printf("> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) break;   /* EOF (Ctrl+D) */
        line[strcspn(line, "\r\n")] = '\0';             /* убрать перевод строки */

        if (line[0] == '\0') continue;                  /* пустая строка */

        if (strcasecmp(line, "exit") == 0 || strcasecmp(line, "quit") == 0) {
            log_event("INFO", "[Поток CLI] команда 'exit' — завершение приложения");
            g_running = 0;                 /* общий флаг остановки для всех потоков */

            /* Обязательно будим ожидающие потоки, иначе они «зависнут»
             * в cond_timedwait до истечения таймаута. */
            pthread_mutex_lock(&g_param_mutex);
            pthread_cond_broadcast(&g_data_ready);
            pthread_cond_broadcast(&g_param_changed);
            pthread_mutex_unlock(&g_param_mutex);
            break;
        }
        if (strcasecmp(line, "help") == 0)  { print_help(); continue; }
        if (strcasecmp(line, "threshold") == 0) {
            pthread_mutex_lock(&g_param_mutex);
            printf("Текущий VIP-порог: %.2f руб.\n", g_vip_threshold);
            pthread_mutex_unlock(&g_param_mutex);
            continue;
        }

        /* Пробуем распарсить число — это новый порог VIP */
        char *endp = NULL;
        double v = strtod(line, &endp);
        while (endp && *endp && isspace((unsigned char)*endp)) endp++;
        if (endp && *endp == '\0' && v >= 0.0) {
            /* КРИТИЧЕСКАЯ СЕКЦИЯ параметров: запись порога и увеличение
             * «поколения» выполняются под одним мьютексом — читатель
             * (VIP-поток) никогда не увидит рассогласованные значения. */
            pthread_mutex_lock(&g_param_mutex);
            g_vip_threshold = v;
            g_threshold_gen++;
            /* БУДИМ ВСЕ аналитические потоки пересчитаться с новым порогом */
            pthread_cond_broadcast(&g_param_changed);
            pthread_mutex_unlock(&g_param_mutex);

            log_event("PARAM", "[Поток CLI] установлен новый VIP-порог: %.2f руб., "
                      "потоки оповещены (broadcast)", v);
        } else {
            printf("Не распознана команда: '%s'. Введите 'help'.\n", line);
        }
    }

    log_event("INFO", "[Поток CLI] завершён");
    return NULL;
}
