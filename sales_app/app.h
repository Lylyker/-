/* ====================================================================
 * app.h — общий заголовочный файл многопоточного приложения продаж.
 * Содержит структуры данных, глобальные параметры и объявления функций.
 * ==================================================================== */
#ifndef APP_H
#define APP_H

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ----------------------------- Константы ---------------------------- */
#define MAX_PRODUCTS     64    /* максимальное количество товаров          */
#define MAX_CUSTOMERS    128   /* максимальное количество покупателей      */
#define MAX_SALES        10000 /* максимум продаж, загружаемых из файла    */
#define SALES_FILE       "sales.txt"
#define CUSTOMERS_FILE   "customers.txt"
#define PRODUCTS_FILE    "products.txt"
#define LOG_FILE         "app.log"

/* --------------------------- Структуры данных ------------------------ */

/* Товар: ID, название, цена за единицу */
typedef struct {
    int    id;
    char   name[64];
    double price;
} product_t;

/* Покупатель: ID, имя */
typedef struct {
    int  id;
    char name[64];
} customer_t;

/* Запись о продаже: ID продажи, метка времени, ID покупателя, ID товара */
typedef struct {
    long   sale_id;
    time_t timestamp;
    int    customer_id;
    int    product_id;
} sale_t;

/* Контейнеры для загрузки справочников и данных о продажах */
typedef struct { product_t  items[MAX_PRODUCTS];   int count; } product_db_t;
typedef struct { customer_t items[MAX_CUSTOMERS];  int count; } customer_db_t;
typedef struct { sale_t     items[MAX_SALES];      int count; } sales_db_t;

/* ---------------------- Глобальные переменные ------------------------ */
/* Они определены в main.c и доступны всем потокам.                     */

extern pthread_mutex_t g_file_mutex;   /* защита файла sales.txt (критическая секция чтения/записи) */
extern pthread_mutex_t g_param_mutex;  /* защита глобальных параметров обработки                   */

extern pthread_cond_t  g_data_ready;   /* сигнал: в sales.txt появились новые данные              */
extern pthread_cond_t  g_param_changed;/* сигнал: пользователь изменил порог VIP — пересчёт!     */

extern volatile int    g_running;          /* флаг «приложение работает» (0 = завершать потоки)  */
extern volatile double g_vip_threshold;    /* порог суммы для VIP-клиентов (меняет CLI-поток)    */
extern volatile int    g_sales_version;    /* счётчик версий файла: растёт при каждой новой продаже */
extern volatile long   g_threshold_gen;    /* «поколение» порога: CLI увеличивает его при смене VIP-порога */

/* ------------------------- Прототипы функций ------------------------- */

/* Логирование (logger.c) */
void log_event(const char *level, const char *fmt, ...);

/* Модель данных (data.c) */
void load_or_create_databases(product_db_t *pd, customer_db_t *cd);
int  load_sales(sales_db_t *sd);                    /* вызывается под g_file_mutex */
double sale_price(const product_db_t *pd, int product_id);

/* Поток симуляции продаж (sim_thread.c) */
void *simulation_thread(void *arg);

/* Аналитические потоки (analytics_threads.c) */
void *aggregation_thread(void *arg);   /* текущая сумма всех продаж            */
void *ranking_thread(void *arg);       /* топ-5 популярных и непопулярных      */
void *vip_thread(void *arg);           /* VIP-клиенты выше порога              */

/* CLI-поток (cli_thread.c) */
void *cli_thread(void *arg);

#endif /* APP_H */
