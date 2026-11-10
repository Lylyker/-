/* ====================================================================
 * sim_thread.c — ПОТОК 1: симуляция продаж.
 * Периодически (с случайным интервалом) генерирует случайную продажу
 * и безопасно дописывает её в sales.txt. После записи рассылает сигнал
 * g_data_ready, чтобы аналитические потоки пересчитали результаты.
 * ==================================================================== */
#include "app.h"
#include <errno.h>
#include <unistd.h>

/* Указатели на справочники передаются через аргумент pthread_create */
typedef struct {
    product_db_t  *pd;
    customer_db_t *cd;
} sim_ctx_t;

/* Запись одной продажи в файл под защитой g_file_mutex. */
static void append_sale(long sale_id, int cust_id, int prod_id)
{
    /* КРИТИЧЕСКАЯ СЕКЦИЯ: пока мы пишем, аналитики не читают файл. */
    if (pthread_mutex_lock(&g_file_mutex) != 0) {
        log_event("ERROR", "mutex_lock(file) не удался: %s", strerror(errno));
        return;
    }

    FILE *f = fopen(SALES_FILE, "a");
    if (!f) {
        log_event("ERROR", "Не удалось открыть %s для записи: %s",
                  SALES_FILE, strerror(errno));
        pthread_mutex_unlock(&g_file_mutex);
        return;
    }

    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_buf);

    fprintf(f, "%ld;%s;%d;%d\n", sale_id, ts, cust_id, prod_id);
    fclose(f);

    /* Инкрементируем «версию данных» под тем же мьютексом файла —
     * это гарантирует, что читатель увидит согласованные пару
     * (содержимое файла, номер версии). */
    g_sales_version++;

    pthread_mutex_unlock(&g_file_mutex);

    /* БУДИМ аналитиков: данные изменились. */
    pthread_mutex_lock(&g_param_mutex);
    pthread_cond_broadcast(&g_data_ready);
    pthread_mutex_unlock(&g_param_mutex);

    log_event("SALE", "Продажа #%ld: покупатель %d купил товар %d",
              sale_id, cust_id, prod_id);
}

void *simulation_thread(void *arg)
{
    sim_ctx_t *ctx = (sim_ctx_t *)arg;
    log_event("INFO", "[Поток симуляции] запущен");

    unsigned seed = (unsigned)time(NULL) ^ (unsigned)pthread_self();
    srand(seed);

    long sale_id = 1;
    while (g_running) {
        /* Случайный интервал: от 1 до 4 секунд */
        int delay = 1 + rand() % 4;
        for (int i = 0; i < delay && g_running; ++i)
            sleep(1);                 /* дробим сон, чтобы быстро реагировать на exit */
        if (!g_running) break;

        int cust_id = 1 + rand() % ctx->cd->count;
        int prod_id = 1 + rand() % ctx->pd->count;
        append_sale(sale_id++, cust_id, prod_id);
    }

    log_event("INFO", "[Поток симуляции] завершён");
    return NULL;
}
