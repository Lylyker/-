/* report.h — генерация txt-отчётов по результатам анализа */
#ifndef REPORT_H
#define REPORT_H

#include <stddef.h>
#include "sales_model.h"

/* Результаты анализа продаж */
typedef struct {
    double total_sum;             /* текущая сумма всех продаж */
    size_t total_count;           /* количество продаж */

    /* топ-5 популярных и топ-5 непопулярных товаров (по количеству проданных шт.) */
    struct { int goods_id; long qty; double sum; } top5[5];
    int top5_n;
    struct { int goods_id; long qty; double sum; } bottom5[5];
    int bottom5_n;

    /* покупатели с суммой покупок выше заданного порога */
    struct { int customer_id; double sum; long sales; } rich[256];
    int rich_n;
    double rich_threshold;

    /* тенденции: продажи по дням */
    struct { char day[16]; double day_sum; long day_count; } trend[64];
    int trend_n;
    char trend_conclusion[256];   /* текстовый вывод о тренде */
} analysis_result_t;

/* Сохранить полный отчёт в txt-файл. Возвращает 0 при успехе. */
int report_save(const char *path, const analysis_result_t *res,
                const goods_t *goods, int goods_n,
                const customer_t *customers, int customers_n);

#endif /* REPORT_H */
