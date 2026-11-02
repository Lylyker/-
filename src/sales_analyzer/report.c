/* report.c — формирование и сохранение txt-отчётов */
#include "report.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static const goods_t *find_goods(const goods_t *g, int n, int id)
{
    return goods_find(g, n, id);
}

static const customer_t *find_customer(const customer_t *c, int n, int id)
{
    for (int i = 0; i < n; i++)
        if (c[i].id == id) return &c[i];
    return NULL;
}

int report_save(const char *path, const analysis_result_t *res,
                const goods_t *goods, int goods_n,
                const customer_t *customers, int customers_n)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;

    time_t now = time(NULL);
    char tbuf[32];
    fmt_time(now, tbuf, sizeof(tbuf));

    fprintf(f, "=====================================================\n");
    fprintf(f, "        ОТЧЁТ ПО ПРОДАЖАМ\n");
    fprintf(f, "        Сформирован: %s\n", tbuf);
    fprintf(f, "=====================================================\n\n");

    fprintf(f, "1. ОБЩИЕ ИТОГИ\n");
    fprintf(f, "   Количество продаж: %zu\n", res->total_count);
    fprintf(f, "   Текущая сумма всех продаж: %.2f руб.\n\n", res->total_sum);

    fprintf(f, "2. ТОП-5 САМЫХ ПОПУЛЯРНЫХ ТОВАРОВ (по шт.)\n");
    for (int i = 0; i < res->top5_n; i++) {
        const goods_t *g = find_goods(goods, goods_n, res->top5[i].goods_id);
        fprintf(f, "   %d. [T%03d] %-30s продано: %5ld шт., на сумму: %12.2f руб.\n",
                i + 1, res->top5[i].goods_id,
                g ? g->name : "(неизвестный товар)",
                res->top5[i].qty, res->top5[i].sum);
    }
    fputc('\n', f);

    fprintf(f, "3. ТОП-5 САМЫХ НЕПОПУЛЯРНЫХ ТОВАРОВ (по шт.)\n");
    for (int i = 0; i < res->bottom5_n; i++) {
        const goods_t *g = find_goods(goods, goods_n, res->bottom5[i].goods_id);
        fprintf(f, "   %d. [T%03d] %-30s продано: %5ld шт., на сумму: %12.2f руб.\n",
                i + 1, res->bottom5[i].goods_id,
                g ? g->name : "(неизвестный товар)",
                res->bottom5[i].qty, res->bottom5[i].sum);
    }
    fputc('\n', f);

    fprintf(f, "4. ПОКУПАТЕЛИ С СУММОЙ ПОКУПОК БОЛЕЕ %.2f РУБ.\n",
            res->rich_threshold);
    if (res->rich_n == 0)
        fprintf(f, "   (таких покупателей нет)\n");
    for (int i = 0; i < res->rich_n; i++) {
        const customer_t *c = find_customer(customers, customers_n,
                                            res->rich[i].customer_id);
        fprintf(f, "   [C%03d] %-30s покупок: %4ld, сумма: %12.2f руб.\n",
                res->rich[i].customer_id,
                c ? c->name : "(неизвестный покупатель)",
                res->rich[i].sales, res->rich[i].sum);
    }
    fputc('\n', f);

    fprintf(f, "5. ТЕНДЕНЦИИ ПРОДАЖ (по дням)\n");
    fprintf(f, "   %-12s %14s %10s\n", "Дата", "Сумма, руб.", "Продаж");
    for (int i = 0; i < res->trend_n; i++)
        fprintf(f, "   %-12s %14.2f %10ld\n",
                res->trend[i].day, res->trend[i].day_sum, res->trend[i].day_count);
    fprintf(f, "   Вывод: %s\n", res->trend_conclusion);

    fprintf(f, "\n=====================================================\n");
    fprintf(f, " Конец отчёта\n");
    fclose(f);
    return 0;
}
