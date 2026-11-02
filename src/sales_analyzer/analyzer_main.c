/* analyzer_main.c — вторая программа: многопоточная обработка информации о продажах.
 *
 * Потоки программы:
 *   1. Поток загрузки данных      (thread_loader)     — читает файл продаж и обновляет данные;
 *   2. Поток "текущая сумма"      (thread_total_sum);
 *   3. Поток "топ-5/анти-топ-5"   (thread_top_goods);
 *   4. Поток "крупные покупатели" (thread_rich_customers);
 *   5. Поток "тенденции продаж"   (thread_trends);
 *   6. Основной поток             — интерактивное меню (пользователь меняет параметры
 *                                    обработки в процессе работы, запускает анализ и отчёты).
 *
 * Исполнитель модуля: студент группы ... (ФИО) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "sales_model.h"
#include "logger.h"
#include "analyzer.h"
#include "report.h"

#define DEFAULT_SALES_FILE     "data/sales.csv"
#define DEFAULT_GOODS_FILE     "data/goods.csv"
#define DEFAULT_CUSTOMERS_FILE "data/customers.csv"
#define LOG_FILE               "logs/analyzer.log"

#define LOADER_NAME "loader"

typedef struct {
    sales_list_t *sl;
    volatile int  reload_flag;    /* запрос перезагрузки из файла (из меню) */
    volatile int  stop_flag;      /* сигнал завершения потока-загрузчика */
    const char   *sales_path;
} loader_ctx_t;

/* Поток загрузки: периодически перечитывает файл продаж,
 * обеспечивая актуальность данных, пока пользователь работает с меню. */
static void *thread_loader(void *arg)
{
    loader_ctx_t *lc = arg;
    while (!lc->stop_flag) {
        if (__atomic_exchange_n(&lc->reload_flag, 0, __ATOMIC_SEQ_CST)) {
            sales_list_t fresh;
            sales_list_init(&fresh);
            int n = sales_file_load(lc->sales_path, &fresh);
            if (n >= 0) {
                pthread_mutex_lock(&lc->sl->mtx);
                free(lc->sl->items);
                lc->sl->items = fresh.items;
                lc->sl->count = fresh.count;
                lc->sl->capacity = fresh.capacity;
                lc->sl->next_sale_id = fresh.next_sale_id;
                pthread_mutex_unlock(&lc->sl->mtx);
                LOG_I(LOADER_NAME, "Перезагружено записей о продажах: %d", n);
            } else {
                LOG_E(LOADER_NAME, "Не удалось загрузить файл %s", lc->sales_path);
                sales_list_free(&fresh);
            }
        }
        usleep(200000);   /* проверка раз в 200 мс */
    }
    LOG_I(LOADER_NAME, "Поток загрузки завершил работу");
    return NULL;
}

/* Запуск четырёх потоков анализа по снимку данных и ожидание их завершения. */
static void run_analysis(sales_list_t *sl, const goods_t *goods, int gn,
                         const customer_t *cust, int cn, double threshold,
                         analysis_result_t *out)
{
    size_t n = 0;
    sale_t *snap = sales_list_snapshot(sl, &n);

    analysis_ctx_t ctx = {
        .sales = snap, .n = n,
        .goods = goods, .goods_n = gn,
        .customers = cust, .customers_n = cn,
        .rich_threshold = threshold,
        .out = out
    };
    memset(out, 0, sizeof(*out));

    pthread_t t1, t2, t3, t4;
    LOG_I("main", "Запуск потоков анализа: total_sum, top_goods, rich_customers, trends");
    pthread_create(&t1, NULL, thread_total_sum,    &ctx);
    pthread_create(&t2, NULL, thread_top_goods,    &ctx);
    pthread_create(&t3, NULL, thread_rich_customers, &ctx);
    pthread_create(&t4, NULL, thread_trends,       &ctx);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    pthread_join(t3, NULL);
    pthread_join(t4, NULL);

    free(snap);
}

static void print_summary(const analysis_result_t *r,
                          const goods_t *goods, int gn,
                          const customer_t *cust, int cn)
{
    printf("\n----- РЕЗУЛЬТАТЫ АНАЛИЗА -----\n");
    printf("Продаж: %zu, текущая сумма всех продаж: %.2f руб.\n",
           r->total_count, r->total_sum);

    printf("\nТоп-5 популярных товаров:\n");
    for (int i = 0; i < r->top5_n; i++) {
        const goods_t *g = goods_find(goods, gn, r->top5[i].goods_id);
        printf("  %d. T%03d %-28s %5ld шт. (%.2f руб.)\n", i + 1,
               r->top5[i].goods_id, g ? g->name : "?",
               r->top5[i].qty, r->top5[i].sum);
    }
    printf("\nТоп-5 непопулярных товаров:\n");
    for (int i = 0; i < r->bottom5_n; i++) {
        const goods_t *g = goods_find(goods, gn, r->bottom5[i].goods_id);
        printf("  %d. T%03d %-28s %5ld шт. (%.2f руб.)\n", i + 1,
               r->bottom5[i].goods_id, g ? g->name : "?",
               r->bottom5[i].qty, r->bottom5[i].sum);
    }

    printf("\nПокупатели с суммой более %.2f руб.:\n", r->rich_threshold);
    if (r->rich_n == 0) printf("  (нет)\n");
    for (int i = 0; i < r->rich_n; i++) {
        const customer_t *c = NULL;
        for (int j = 0; j < cn; j++)
            if (cust[j].id == r->rich[i].customer_id) { c = &cust[j]; break; }
        printf("  C%03d %-28s %ld покупок, %.2f руб.\n",
               r->rich[i].customer_id, c ? c->name : "?",
               r->rich[i].sales, r->rich[i].sum);
    }

    printf("\nТенденции (по дням):\n");
    for (int i = 0; i < r->trend_n; i++)
        printf("  %s: %.2f руб., %ld продаж\n",
               r->trend[i].day, r->trend[i].day_sum, r->trend[i].day_count);
    printf("  Вывод: %s\n", r->trend_conclusion);
    printf("------------------------------\n");
}

static void menu(void)
{
    printf("\n===== МЕНЮ АНАЛИЗАТОРА ПРОДАЖ =====\n"
           "  1. Показать текущие данные (кол-во продаж)\n"
           "  2. Запустить многопоточный анализ\n"
           "  3. Изменить порог суммы для крупных покупателей\n"
           "  4. Перезагрузить данные из файла продаж\n"
           "  5. Сохранить отчёт в txt-файл\n"
           "  6. Справка о потоках программы\n"
           "  0. Выход\n"
           "Выбор: ");
}

int main(int argc, char **argv)
{
    const char *sales_file = (argc > 1) ? argv[1] : DEFAULT_SALES_FILE;
    const char *goods_file = (argc > 2) ? argv[2] : DEFAULT_GOODS_FILE;
    const char *cust_file  = (argc > 3) ? argv[3] : DEFAULT_CUSTOMERS_FILE;

    static goods_t goods[MAX_GOODS];
    static customer_t customers[MAX_CUSTOMERS];
    int gn = goods_load(goods_file, goods, MAX_GOODS);
    int cn = customers_load(cust_file, customers, MAX_CUSTOMERS);
    if (gn < 0 || cn < 0) {
        fprintf(stderr, "Ошибка загрузки справочников: %s / %s\n",
                goods_file, cust_file);
        return 1;
    }

    if (logger_init(LOG_FILE) != 0)
        fprintf(stderr, "Внимание: не открыт файл логов %s\n", LOG_FILE);
    LOG_I("main", "Анализатор запущен. Файл продаж: %s", sales_file);

    sales_list_t sl;
    sales_list_init(&sl);
    int n0 = sales_file_load(sales_file, &sl);
    if (n0 < 0)
        LOG_W("main", "Файл продаж %s пока недоступен", sales_file);
    else
        LOG_I("main", "Загружено начальных продаж: %d", n0);
    printf("Загружено записей о продажах: %d, товаров: %d, покупателей: %d\n",
           n0 < 0 ? 0 : n0, gn, cn);

    /* Параметры обработки (меняются пользователем в процессе работы) */
    double threshold = 10000.0;

    /* Поток фонового обновления данных */
    loader_ctx_t lc = { .sl = &sl, .reload_flag = 0, .stop_flag = 0,
                        .sales_path = sales_file };
    pthread_t loader_tid;
    pthread_create(&loader_tid, NULL, thread_loader, &lc);

    analysis_result_t last_result;
    int have_result = 0;
    memset(&last_result, 0, sizeof(last_result));

    char line[256];
    int running = 1;
    while (running) {
        menu();
        if (!fgets(line, sizeof(line), stdin)) break;
        int choice = atoi(line);
        switch (choice) {
        case 1:
            printf("В памяти сейчас %zu продаж.\n", sl.count);
            break;
        case 2:
            run_analysis(&sl, goods, gn, customers, cn, threshold, &last_result);
            have_result = 1;
            print_summary(&last_result, goods, gn, customers, cn);
            break;
        case 3: {
            printf("Новый порог суммы (руб.): ");
            if (fgets(line, sizeof(line), stdin)) {
                threshold = atof(line);
                printf("Параметр изменён: порог = %.2f руб. "
                       "(применится при следующем анализе).\n", threshold);
                LOG_I("main", "Пользователь изменил порог: %.2f", threshold);
            }
            break;
        }
        case 4:
            __atomic_store_n(&lc.reload_flag, 1, __ATOMIC_SEQ_CST);
            printf("Запрошена перезагрузка данных из файла %s "
                   "(поток loader выполнит её асинхронно).\n", sales_file);
            sleep(1);
            printf("Текущее число продаж: %zu\n", sl.count);
            break;
        case 5: {
            if (!have_result) {
                printf("Сначала выполните анализ (пункт 2).\n");
                break;
            }
            printf("Имя файла отчёта [reports/report.txt]: ");
            char path[128] = {0};
            if (!fgets(path, sizeof(path), stdin) || path[0] == '\n')
                strcpy(path, "reports/report.txt");
            else
                path[strcspn(path, "\r\n")] = '\0';
            if (report_save(path, &last_result, goods, gn, customers, cn) == 0) {
                printf("Отчёт сохранён: %s\n", path);
                LOG_I("main", "Отчёт сохранён в %s", path);
            } else {
                printf("Ошибка сохранения отчёта.\n");
                LOG_E("main", "Ошибка сохранения отчёта в %s", path);
            }
            break;
        }
        case 6:
            printf("Многопоточная структура анализатора:\n"
                   "  - поток loader          : фоновая перезагрузка файла продаж;\n"
                   "  - поток total_sum       : расчёт текущей суммы всех продаж;\n"
                   "  - поток top_goods       : топ-5 популярных и непопулярных товаров;\n"
                   "  - поток rich_customers  : покупатели с суммой выше порога;\n"
                   "  - поток trends          : тенденция продаж по дням;\n"
                   "  - основной поток        : интерактивное меню параметров.\n");
            break;
        case 0:
            running = 0;
            break;
        default:
            printf("Неизвестная команда.\n");
        }
    }

    lc.stop_flag = 1;
    pthread_join(loader_tid, NULL);
    sales_list_free(&sl);
    LOG_I("main", "Анализатор завершил работу");
    logger_close();
    return 0;
}
