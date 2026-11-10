/* ====================================================================
 * analytics_threads.c — ПОТОКИ 2, 3, 4: агрегация, ранжирование, VIP.
 * Все три потока работают по одному сценарию:
 *   1) ждут сигнал g_data_ready (новые продажи) или g_param_changed
 *      (изменился порог) с таймаутом, чтобы периодически обновляться
 *      даже без событий;
 *   2) читают sales.txt под защитой g_file_mutex;
 *   3) считают свой показатель и печатают его.
 * ==================================================================== */
#include "app.h"
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>

/* Общий контекст передается всем аналитикам через pthread_create */
typedef struct {
    product_db_t  *pd;
    customer_db_t *cd;
} an_ctx_t;

/* Строит абсолютное время ожидания (sec секунд от текущего момента)
 * для pthread_cond_timedwait. */
static void make_timeout(struct timespec *ts, int sec)
{
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += sec;
}

/* Ожидание события: новые данные (g_data_ready) ИЛИ смена порога
 * (g_param_changed) ИЛИ плановый таймаут.
 * Логика синхронизации:
 *  - cond_wait ВСЕГДА вызывается при занятом мьютексе g_param_mutex;
 *  - на каждом витке проверяем «поколение» порогов g_threshold_gen:
 *    если оно изменилось — сразу идём пересчитываться;
 *  - ожидание обоих условий чередуется короткими таймбоксами, поэтому
 *    поток просыпается и по broadcast любого из двух условий.
 * Возвращает 1 — всегда пора что-то пересчитать (событие или таймаут). */
static int wait_for_event(int timeout_sec, int *seen_version)
{
    long gen0 = g_threshold_gen;   /* поколение порога на момент входа */

    pthread_mutex_lock(&g_param_mutex);
    if (g_sales_version != *seen_version || g_threshold_gen != gen0) {
        goto done;                 /* событие уже произошло во время расчётов */
    }
    while (g_running &&
           g_sales_version == *seen_version &&
           g_threshold_gen == gen0)
    {
        /* Ждём короткими квантами по каждому условию по очереди:
         * поток проснётся на broadcast как g_data_ready, так и g_param_changed,
         * а также сработает плановый таймаут. */
        struct timespec ts;
        make_timeout(&ts, 1);
        pthread_cond_timedwait(&g_data_ready, &g_param_mutex, &ts);
        if (g_sales_version != *seen_version || g_threshold_gen != gen0) break;

        make_timeout(&ts, 1);
        pthread_cond_timedwait(&g_param_changed, &g_param_mutex, &ts);
        if (g_sales_version != *seen_version || g_threshold_gen != gen0) break;

        timeout_sec--;             /* не чаще раза в N секунд без событий */
        if (timeout_sec <= 0) break;
    }
done:
    *seen_version = g_sales_version;  /* фиксируем: эти данные поток «съел» */
    pthread_mutex_unlock(&g_param_mutex);
    return 1;
}

/* Чтение файла продаж с блокировкой. Буфер sd выделяется один раз
 * вызывающим (статически/на стеке), чтобы не грузить кучу каждые N сек. */
static int read_sales_safe(sales_db_t *sd)
{
    if (pthread_mutex_lock(&g_file_mutex) != 0) {
        log_event("ERROR", "mutex_lock(file) не удался: %s", strerror(errno));
        return -1;
    }
    int n = load_sales(sd);
    pthread_mutex_unlock(&g_file_mutex);
    return n;
}

/* ------------------- ПОТОК 2: текущая сумма всех продаж ------------------ */
void *aggregation_thread(void *arg)
{
    an_ctx_t *ctx = (an_ctx_t *)arg;
    static sales_db_t sd;   /* большой буфер — статически, не на стеке */
    int seen = -1;              /* версия данных, обработанная этим потоком */
    log_event("INFO", "[Поток агрегации] запущен");

    while (g_running) {
        wait_for_event(5, &seen);            /* пересчёт не реже, чем раз в 5 секунд */
        if (!g_running) break;
        if (read_sales_safe(&sd) < 0) continue;

        double total = 0.0;
        for (int i = 0; i < sd.count; ++i)
            total += sale_price(ctx->pd, sd.items[i].product_id);

        log_event("TOTAL", "[Агрегация] продаж: %d, текущая сумма всех продаж: %.2f руб.",
                  sd.count, total);
    }
    log_event("INFO", "[Поток агрегации] завершён");
    return NULL;
}

/* ------------------- ПОТОК 3: топ-5 популярных/непопулярных -------------- */
void *ranking_thread(void *arg)
{
    an_ctx_t *ctx = (an_ctx_t *)arg;
    static sales_db_t sd;
    int seen = -1;
    log_event("INFO", "[Поток ранжирования] запущен");

    while (g_running) {
        wait_for_event(8, &seen);
        if (!g_running) break;
        if (read_sales_safe(&sd) < 0) continue;

        /* Счётчики числа покупок по каждому товару из справочника */
        int cnt[MAX_PRODUCTS];
        memset(cnt, 0, sizeof(cnt));
        for (int i = 0; i < sd.count; ++i)
            for (int j = 0; j < ctx->pd->count; ++j)
                if (sd.items[i].product_id == ctx->pd->items[j].id)
                    { cnt[j]++; break; }

        int n = ctx->pd->count;
        int order[MAX_PRODUCTS];            /* индексы товаров для сортировки */
        for (int i = 0; i < n; ++i) order[i] = i;

        /* Полная сортировка выбором по убыванию количества продаж.
         * Товаров немного (<= MAX_PRODUCTS), поэтому O(n^2) оправдана. */
        for (int k = 0; k < n - 1; ++k) {
            int best = k;
            for (int i = k + 1; i < n; ++i)
                if (cnt[order[i]] > cnt[order[best]]) best = i;
            if (best != k) {
                int t = order[k]; order[k] = order[best]; order[best] = t;
            }
        }

        int top = n < 5 ? n : 5;
        printf("\n===== ТОП-5 самых ПОПУЛЯРНЫХ товаров =====\n");
        for (int k = 0; k < top; ++k)               /* начало = максимумы */
            printf("  %d. %-20s — %d продаж\n", k + 1,
                   ctx->pd->items[order[k]].name, cnt[order[k]]);

        printf("===== ТОП-5 самых НЕПОПУЛЯРНЫХ товаров =====\n");
        for (int k = 0; k < top; ++k) {             /* конец = минимумы */
            int idx = n - 1 - k;
            printf("  %d. %-20s — %d продаж\n", k + 1,
                   ctx->pd->items[order[idx]].name, cnt[order[idx]]);
        }
        fflush(stdout);
    }
    log_event("INFO", "[Поток ранжирования] завершён");
    return NULL;
}

/* ------------------------- ПОТОК 4: VIP-клиенты -------------------------- */
void *vip_thread(void *arg)
{
    an_ctx_t *ctx = (an_ctx_t *)arg;
    static sales_db_t sd;
    int seen = -1;
    log_event("INFO", "[Поток VIP-фильтрации] запущен");

    while (g_running) {
        wait_for_event(6, &seen);
        if (!g_running) break;

        /* Порог читаем под защитой g_param_mutex — его пишет CLI-поток */
        pthread_mutex_lock(&g_param_mutex);
        double threshold = g_vip_threshold;
        pthread_mutex_unlock(&g_param_mutex);

        if (read_sales_safe(&sd) < 0) continue;

        /* Накопленные суммы по покупателям */
        double sum[MAX_CUSTOMERS] = {0};
        for (int i = 0; i < sd.count; ++i) {
            double p = sale_price(ctx->pd, sd.items[i].product_id);
            for (int j = 0; j < ctx->cd->count; ++j)
                if (sd.items[i].customer_id == ctx->cd->items[j].id)
                    { sum[j] += p; break; }
        }

        printf("\n===== VIP-клиенты (сумма покупок > %.2f руб.) =====\n", threshold);
        int found = 0;
        for (int j = 0; j < ctx->cd->count; ++j)
            if (sum[j] > threshold) {
                printf("  %s (ID %d) — набрал на %.2f руб.\n",
                       ctx->cd->items[j].name, ctx->cd->items[j].id, sum[j]);
                found++;
            }
        if (!found) printf("  (таких покупателей пока нет)\n");
        fflush(stdout);
        log_event("VIP", "[VIP] порог=%.2f руб., таких клиентов: %d", threshold, found);
    }
    log_event("INFO", "[Поток VIP-фильтрации] завершён");
    return NULL;
}
