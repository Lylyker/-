/* analyzer.c — реализация функций, выполняемых в отдельных потоках.
 * Каждая функция пишет только в свой фрагмент структуры результатов,
 * поэтому потокам достаточно общего read-only доступа к снимку продаж. */
#include "analyzer.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define THREAD_NAME "analysis"

/* ---------- Поток 1: текущая сумма всех продаж ---------- */
void *thread_total_sum(void *arg)
{
    analysis_ctx_t *ctx = arg;
    double sum = 0.0;

    for (size_t i = 0; i < ctx->n; i++)
        sum += ctx->sales[i].amount;

    ctx->out->total_sum = sum;
    ctx->out->total_count = ctx->n;
    LOG_I(THREAD_NAME, "Поток total_sum: обработано %zu продаж, сумма = %.2f",
          ctx->n, sum);
    return NULL;
}

/* ---------- Поток 2: топ-5 популярных и непопулярных товаров ---------- */

typedef struct { int id; long qty; double sum; } agg_t;

static int agg_cmp_qty_desc(const void *a, const void *b)
{
    const agg_t *x = a, *y = b;
    if (y->qty != x->qty) return (y->qty > x->qty) ? 1 : -1;
    return x->id - y->id;
}

void *thread_top_goods(void *arg)
{
    analysis_ctx_t *ctx = arg;
    agg_t *agg = calloc(ctx->goods_n > 0 ? ctx->goods_n : 1, sizeof(*agg));
    if (!agg) return NULL;
    int na = 0;

    /* агрегация по товарам */
    for (size_t i = 0; i < ctx->n; i++) {
        int found = -1;
        for (int j = 0; j < na; j++)
            if (agg[j].id == ctx->sales[i].goods_id) { found = j; break; }
        if (found < 0 && na < ctx->goods_n) {
            agg[na].id = ctx->sales[i].goods_id;
            agg[na].qty = 0; agg[na].sum = 0;
            found = na++;
        }
        if (found >= 0) {
            agg[found].qty += ctx->sales[i].qty;
            agg[found].sum += ctx->sales[i].amount;
        }
    }

    qsort(agg, na, sizeof(*agg), agg_cmp_qty_desc);

    int k = na < 5 ? na : 5;
    ctx->out->top5_n = k;
    for (int i = 0; i < k; i++) {
        ctx->out->top5[i].goods_id = agg[i].id;
        ctx->out->top5[i].qty = agg[i].qty;
        ctx->out->top5[i].sum = agg[i].sum;
    }
    /* непопулярные — с конца списка */
    int kb = na < 5 ? na : 5;
    ctx->out->bottom5_n = kb;
    for (int i = 0; i < kb; i++) {
        const agg_t *g = &agg[na - 1 - i];
        ctx->out->bottom5[i].goods_id = g->id;
        ctx->out->bottom5[i].qty = g->qty;
        ctx->out->bottom5[i].sum = g->sum;
    }

    LOG_I(THREAD_NAME, "Поток top_goods: товаров в статистике = %d", na);
    free(agg);
    return NULL;
}

/* ---------- Поток 3: покупатели с суммой выше порога ---------- */
void *thread_rich_customers(void *arg)
{
    analysis_ctx_t *ctx = arg;

    typedef struct { int id; double sum; long sales; } cagg_t;
    cagg_t *ca = calloc(ctx->customers_n > 0 ? ctx->customers_n : 1, sizeof(*ca));
    if (!ca) return NULL;
    int nc = 0;

    for (size_t i = 0; i < ctx->n; i++) {
        int found = -1;
        for (int j = 0; j < nc; j++)
            if (ca[j].id == ctx->sales[i].customer_id) { found = j; break; }
        if (found < 0 && nc < ctx->customers_n) {
            ca[nc].id = ctx->sales[i].customer_id;
            ca[nc].sum = 0; ca[nc].sales = 0;
            found = nc++;
        }
        if (found >= 0) {
            ca[found].sum += ctx->sales[i].amount;
            ca[found].sales++;
        }
    }

    int cnt = 0;
    for (int j = 0; j < nc; j++) {
        if (ca[j].sum > ctx->rich_threshold &&
            cnt < (int)(sizeof(ctx->out->rich)/sizeof(ctx->out->rich[0]))) {
            ctx->out->rich[cnt].customer_id = ca[j].id;
            ctx->out->rich[cnt].sum = ca[j].sum;
            ctx->out->rich[cnt].sales = ca[j].sales;
            cnt++;
        }
    }
    /* сортировка по убыванию суммы (простая вставками — элементов немного) */
    for (int i = 1; i < cnt; i++) {
        __typeof__(ctx->out->rich[0]) key = ctx->out->rich[i];
        int k = i - 1;
        while (k >= 0 && ctx->out->rich[k].sum < key.sum) {
            ctx->out->rich[k+1] = ctx->out->rich[k];
            k--;
        }
        ctx->out->rich[k+1] = key;
    }
    ctx->out->rich_n = cnt;
    ctx->out->rich_threshold = ctx->rich_threshold;

    LOG_I(THREAD_NAME,
          "Поток rich_customers: порог = %.2f, крупных покупателей = %d",
          ctx->rich_threshold, cnt);
    free(ca);
    return NULL;
}

/* ---------- Поток 4: тенденции продаж по дням ---------- */

typedef struct { long day; double sum; long count; } dayagg_t;

static long day_key(time_t ts)
{
    struct tm tm_buf;
    localtime_r(&ts, &tm_buf);
    tm_buf.tm_hour = tm_buf.tm_min = tm_buf.tm_sec = 0;
    return (long)mktime(&tm_buf);
}

static int day_cmp_asc(const void *a, const void *b)
{
    const dayagg_t *x = a, *y = b;
    if (x->day < y->day) return -1;
    if (x->day > y->day) return 1;
    return 0;
}

void *thread_trends(void *arg)
{
    analysis_ctx_t *ctx = arg;
    dayagg_t *days = calloc(ctx->n > 0 ? ctx->n : 1, sizeof(*days));
    if (!days) return NULL;
    int nd = 0;

    for (size_t i = 0; i < ctx->n; i++) {
        long d = day_key(ctx->sales[i].ts);
        int found = -1;
        for (int j = 0; j < nd; j++)
            if (days[j].day == d) { found = j; break; }
        if (found < 0) {
            days[nd].day = d; days[nd].sum = 0; days[nd].count = 0;
            found = nd++;
        }
        days[found].sum += ctx->sales[i].amount;
        days[found].count++;
    }

    qsort(days, nd, sizeof(*days), day_cmp_asc);

    int lim = nd < 64 ? nd : 64;
    ctx->out->trend_n = lim;
    for (int i = 0; i < lim; i++) {
        char tbuf[16];
        time_t t = (time_t)days[i].day;
        struct tm tm_buf;
        localtime_r(&t, &tm_buf);
        strftime(tbuf, sizeof(tbuf), "%Y-%m-%d", &tm_buf);
        snprintf(ctx->out->trend[i].day, sizeof(ctx->out->trend[i].day),
                 "%s", tbuf);
        ctx->out->trend[i].day_sum = days[i].sum;
        ctx->out->trend[i].day_count = days[i].count;
    }

    /* вывод о тренде: сравнение второй половины периода с первой */
    const char *concl = "Недостаточно данных для определения тенденции.";
    if (lim >= 2) {
        double first = 0, second = 0;
        int half = lim / 2;
        for (int i = 0; i < half; i++) first += days[i].sum;
        for (int i = half; i < lim; i++) second += days[i].sum;
        double growth = (first > 0) ? (second - first) / first * 100.0 : 0.0;
        if (growth > 5.0)
            snprintf(ctx->out->trend_conclusion,
                     sizeof(ctx->out->trend_conclusion),
                     "Продажи растут: вторая половина периода на %.1f%% "
                     "выше первой.", growth);
        else if (growth < -5.0)
            snprintf(ctx->out->trend_conclusion,
                     sizeof(ctx->out->trend_conclusion),
                     "Продажи снижаются: вторая половина периода на %.1f%% "
                     "ниже первой.", -growth);
        else
            snprintf(ctx->out->trend_conclusion,
                     sizeof(ctx->out->trend_conclusion),
                     "Продажи стабильны: изменение между половинами "
                     "периода составляет %.1f%%.", growth);
        (void)concl;
    } else {
        strncpy(ctx->out->trend_conclusion, concl,
                sizeof(ctx->out->trend_conclusion) - 1);
    }

    LOG_I(THREAD_NAME, "Поток trends: дней с продажами = %d", nd);
    free(days);
    return NULL;
}
