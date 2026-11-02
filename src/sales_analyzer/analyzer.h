/* analyzer.h — интерфейсы модулей-потоков анализа */
#ifndef ANALYZER_H
#define ANALYZER_H

#include "report.h"

/* Контекст, разделяемый потоками анализа. */
typedef struct {
    sale_t             *sales;        /* снимок данных (разделён между потоками) */
    size_t              n;
    const goods_t      *goods;
    int                 goods_n;
    const customer_t   *customers;
    int                 customers_n;

    /* параметры обработки (можно менять в процессе работы) */
    double              rich_threshold;   /* порог суммы для "крупных" покупателей */

    /* результаты */
    analysis_result_t  *out;
} analysis_ctx_t;

/* Функции потоков-анализаторов (запускаются через pthread_create): */
void *thread_total_sum(void   *arg);   /* текущая сумма всех продаж */
void *thread_top_goods(void    *arg);  /* топ-5 популярных/непопулярных */
void *thread_rich_customers(void *arg);/* покупатели выше порога */
void *thread_trends(void       *arg);  /* тенденции продаж по дням */

#endif /* ANALYZER_H */
