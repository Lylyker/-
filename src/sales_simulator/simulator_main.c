/* simulator_main.c — первая программа: имитация продаж (многопоточная).
 *
 * Каждый поток имитирует продажу ОДНОГО товара через случайные интервалы
 * времени. Проданный товар записывается в файл продаж (data/sales.csv)
 * и дублируется в оперативный список (для отладки/статистики потока).
 *
 * Потоки программы:
 *   - по одному потоку sales_thread на каждый товар из goods.csv;
 *   - основной поток — управление (количество продаж на поток, длительность пауз).
 *
 * Исполнитель модуля: студент группы ... (ФИО) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>

#include "sales_model.h"
#include "logger.h"

#define DEFAULT_SALES_FILE "data/sales.csv"
#define DEFAULT_GOODS_FILE "data/goods.csv"
#define DEFAULT_CUST_FILE  "data/customers.csv"
#define LOG_FILE           "logs/simulator.log"

typedef struct {
    const goods_t   *goods;        /* товар, который «продаёт» этот поток */
    const customer_t *customers;
    int              customers_n;
    sales_list_t     *sl;          /* общий список (для ID и статистики) */
    const char       *sales_path;
    int               sales_count; /* сколько продаж имитировать */
    unsigned          seed;        /* собственный ГСЧ потока */
    long              sold_qty;    /* итог: сколько штук «продал» */
    double            sold_sum;    /* итог: сумма продаж потока */
} seller_ctx_t;

static void thread_name(const goods_t *g, char *buf, size_t n)
{
    snprintf(buf, n, "seller-T%03d", g->id);
}

/* Поток-продавец: делает sales_count продаж с случайными интервалами. */
static void *sales_thread(void *arg)
{
    seller_ctx_t *c = arg;
    char tname[MAX_NAME_LEN];
    thread_name(c->goods, tname, sizeof(tname));

    LOG_I(tname, "Поток запущен: товар [%s] цена %.2f руб., план продаж: %d",
          c->goods->name, c->goods->price, c->sales_count);

    for (int i = 0; i < c->sales_count; i++) {
        /* случайный интервал «времени работы магазина»: 50..400 мс */
        unsigned interval = 50 + (c->seed = c->seed * 1103515245u + 12345u,
                                  (c->seed >> 16) % 351u);
        usleep(interval * 1000u);

        int qty = 1 + (int)((c->seed = c->seed * 1103515245u + 12345u,
                             (c->seed >> 16) % 5u));
        int cust_idx = (int)((c->seed = c->seed * 1103515245u + 12345u,
                              (c->seed >> 16) % (unsigned)c->customers_n));
        double amount = qty * c->goods->price;
        time_t now = time(NULL);

        sale_t rec = {
            .ts = now,
            .customer_id = c->customers[cust_idx].id,
            .goods_id = c->goods->id,
            .qty = qty,
            .amount = amount
        };

        /* Единая блокировка: генерация уникального ID, добавление в общий
         * список и запись в файл продаж — операция атомарна для потока. */
        pthread_mutex_lock(&c->sl->mtx);
        rec.sale_id = c->sl->next_sale_id++;
        if (sales_list_push(c->sl, &rec) != 0)
            LOG_E(tname, "Ошибка выделения памяти в общем списке продаж");
        if (sales_file_append(c->sales_path, &rec) != 0)
            LOG_E(tname, "Ошибка записи в файл продаж");
        pthread_mutex_unlock(&c->sl->mtx);

        c->sold_qty += qty;
        c->sold_sum += amount;
        LOG_D(tname, "Продажа #%ld: %d шт. покупателю C%03d, сумма %.2f, пауза %u мс",
              rec.sale_id, qty, c->customers[cust_idx].id, amount, interval);
    }

    LOG_I(tname, "Поток завершил работу: продано %ld шт. на %.2f руб.",
          c->sold_qty, c->sold_sum);
    return NULL;
}

int main(int argc, char **argv)
{
    const char *sales_file = (argc > 1) ? argv[1] : DEFAULT_SALES_FILE;
    const char *goods_file = (argc > 2) ? argv[2] : DEFAULT_GOODS_FILE;
    const char *cust_file  = (argc > 3) ? argv[3] : DEFAULT_CUST_FILE;
    int per_product        = (argc > 4) ? atoi(argv[4]) : 8; /* продаж на товар */

    if (per_product < 1) per_product = 1;

    static goods_t goods[MAX_GOODS];
    static customer_t customers[MAX_CUSTOMERS];

    int gn = goods_load(goods_file, goods, MAX_GOODS);
    int cn = customers_load(cust_file, customers, MAX_CUSTOMERS);
    if (gn <= 0 || cn <= 0) {
        fprintf(stderr, "Не удалось загрузить справочники (%s / %s)\n",
                goods_file, cust_file);
        return 1;
    }

    if (logger_init(LOG_FILE) != 0)
        fprintf(stderr, "Внимание: не открыт файл логов %s\n", LOG_FILE);
    LOG_I("main", "Симулятор запущен: товаров %d, покупателей %d, "
                  "продаж на товар %d", gn, cn, per_product);

    /* Создаём «чистый» файл продаж */
    if (sales_file_write_header(sales_file) != 0) {
        fprintf(stderr, "Не удалось создать файл продаж %s\n", sales_file);
        LOG_E("main", "Не удалось создать файл продаж %s", sales_file);
        logger_close();
        return 1;
    }

    sales_list_t sl;
    sales_list_init(&sl);

    pthread_t threads[MAX_GOODS];
    static seller_ctx_t ctx[MAX_GOODS];
    time_t start = time(NULL);

    printf("Имитация продаж: %d потоков (по одному на товар), "
           "каждый сделает %d продаж...\n", gn, per_product);

    for (int i = 0; i < gn; i++) {
        ctx[i] = (seller_ctx_t){
            .goods = &goods[i],
            .customers = customers,
            .customers_n = cn,
            .sl = &sl,
            .sales_path = sales_file,
            .sales_count = per_product,
            .seed = (unsigned)(time(NULL) ^ (i * 2654435761u)) | 1u,
        };
        if (pthread_create(&threads[i], NULL, sales_thread, &ctx[i]) != 0) {
            fprintf(stderr, "Не удалось создать поток для товара T%03d\n",
                    goods[i].id);
            ctx[i].sales_count = 0; /* помечаем, что поток не создан */
        }
    }

    /* ожидание завершения всех потоков-продавцов */
    for (int i = 0; i < gn; i++)
        if (ctx[i].sales_count > 0 || threads[i])
            pthread_join(threads[i], NULL);

    double total = 0;
    long total_qty = 0;
    for (int i = 0; i < gn; i++) {
        total += ctx[i].sold_sum;
        total_qty += ctx[i].sold_qty;
    }

    char tbuf[32];
    fmt_time(start, tbuf, sizeof(tbuf));
    printf("\n===== ИТОГИ СИМУЛЯЦИИ =====\n");
    printf("Начало: %s, поток-продавцов: %d\n", tbuf, gn);
    printf("Всего продаж записано: %zu\n", sl.count);
    printf("Всего продано единиц : %ld\n", total_qty);
    printf("Общая выручка        : %.2f руб.\n", total);
    printf("Файл продаж          : %s\n", sales_file);

    LOG_I("main", "Симуляция завершена: продаж %zu, выручка %.2f руб.",
          sl.count, total);

    sales_list_free(&sl);
    logger_close();
    return 0;
}
