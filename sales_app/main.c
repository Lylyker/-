/* ====================================================================
 * main.c — точка входа многопоточного приложения обработки продаж.
 * Создаёт примитивы синхронизации, запускает 5 потоков и корректно
 * дожидается их завершения (pthread_join) по команде "exit".
 * ==================================================================== */
#include "app.h"
#include <errno.h>

/* ---------------- Определение глобальных переменных ------------------ */
pthread_mutex_t g_file_mutex  = PTHREAD_MUTEX_INITIALIZER; /* файл sales.txt   */
pthread_mutex_t g_param_mutex = PTHREAD_MUTEX_INITIALIZER; /* параметры        */
pthread_cond_t  g_data_ready  = PTHREAD_COND_INITIALIZER;  /* новые продажи    */
pthread_cond_t  g_param_changed = PTHREAD_COND_INITIALIZER;/* смена порога     */

volatile int    g_running        = 1;      /* приложение работает            */
volatile double g_vip_threshold  = 50000.0;/* стартовый порог VIP-клиентов   */
volatile int    g_sales_version  = 0;      /* версия данных в файле          */
volatile long   g_threshold_gen  = 0;      /* поколение порога               */

/* Контекст, разделяемый потоками (справочники читаются только после
 * загрузки — до создания потоков, поэтому дальше доступны без блокировок) */
static product_db_t  g_products;
static customer_db_t g_customers;

int main(void)
{
    /* Удаляем старый лог, чтобы записи соответствовали текущему запуску */
    remove(LOG_FILE);
    log_event("INFO", "=== Приложение «Обработка информации о продажах» запущено ===");

    /* Инициализация мьютексов с проверкой кода возврата */
    if (pthread_mutex_init(&g_file_mutex, NULL) != 0) {
        fprintf(stderr, "Не удалось инициализировать мьютекс файла\n");
        return 1;
    }
    if (pthread_mutex_init(&g_param_mutex, NULL) != 0) {
        fprintf(stderr, "Не удалось инициализировать мьютекс параметров\n");
        return 1;
    }
    if (pthread_cond_init(&g_data_ready, NULL) != 0 ||
        pthread_cond_init(&g_param_changed, NULL) != 0) {
        fprintf(stderr, "Не удалось инициализировать условия\n");
        return 1;
    }

    /* Загрузка/генерация тестовых справочников ДО старта потоков */
    load_or_create_databases(&g_products, &g_customers);
    if (g_products.count == 0 || g_customers.count == 0) {
        log_event("ERROR", "Справочники пусты — работа невозможна");
        return 1;
    }

    /* ID_товара и ID_покупателя в продах соответствуют позициям справочника */
    struct timespec ts_start;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);
    (void)ts_start;

    /* ------------------- Создание 5 потоков -------------------------- */
    pthread_t t_sim, t_agg, t_rank, t_vip, t_cli;
    struct { product_db_t *pd; customer_db_t *cd; } ctx =
        { &g_products, &g_customers };

    struct th_spec {
        pthread_t *tid;
        void *(*fn)(void *);
        const char *name;
    } specs[] = {
        { &t_sim,  simulation_thread,  "симуляции продаж"  },
        { &t_agg,  aggregation_thread, "агрегации сумм"    },
        { &t_rank, ranking_thread,     "ранжирования"      },
        { &t_vip,  vip_thread,         "VIP-фильтрации"    },
        { &t_cli,  cli_thread,         "CLI-интерфейса"    },
    };

    for (size_t i = 0; i < sizeof(specs)/sizeof(specs[0]); ++i) {
        int rc = pthread_create(specs[i].tid, NULL, specs[i].fn, &ctx);
        if (rc != 0) {   /* pthread_create возвращает код ошибки, а не errno */
            log_event("ERROR", "Не удалось создать поток %s (код %d: %s)",
                      specs[i].name, rc, strerror(rc));
            g_running = 0;
            /* Дожидаемся уже созданных потоков перед выходом */
            for (size_t k = 0; k < i; ++k) pthread_join(*specs[k].tid, NULL);
            return 1;
        }
        log_event("INFO", "Создан поток: %s", specs[i].name);
    }

    /* --------------------- Ожидание завершения ------------------------ */
    /* Все потоки работают, пока g_running == 1. Команда "exit" из CLI
     * обнуляет флаг и рассылает broadcast — потоки просыпаются и выходят. */
    pthread_join(t_cli,  NULL);
    pthread_join(t_sim,  NULL);
    pthread_join(t_agg,  NULL);
    pthread_join(t_rank, NULL);
    pthread_join(t_vip,  NULL);

    /* Освобождение примитивов */
    pthread_mutex_destroy(&g_file_mutex);
    pthread_mutex_destroy(&g_param_mutex);
    pthread_cond_destroy(&g_data_ready);
    pthread_cond_destroy(&g_param_changed);

    log_event("INFO", "=== Все потоки завершены. Приложение остановлено ===");
    return 0;
}
