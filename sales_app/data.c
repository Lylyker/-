/* ====================================================================
 * data.c — модуль работы с данными: справочники товаров/покупателей
 * и чтение файла продаж. Содержит генерацию тестовых данных.
 * ==================================================================== */
#include "app.h"
#include <errno.h>
#include <unistd.h>

/* ---------- Поиск товара по ID (линейный проход по массиву) ---------- */
static const product_t *find_product(const product_db_t *pd, int id)
{
    for (int i = 0; i < pd->count; ++i)
        if (pd->items[i].id == id)
            return &pd->items[i];
    return NULL;
}

/* Цена продажи = цена товара из справочника; 0 если товар неизвестен */
double sale_price(const product_db_t *pd, int product_id)
{
    const product_t *p = find_product(pd, product_id);
    return p ? p->price : 0.0;
}

/* --------- Генерация тестовых файлов, если они отсутствуют ----------- */
static void generate_default_products(void)
{
    static const struct { const char *name; double price; } demo[] = {
        {"Ноутбук",        55000.0},
        {"Смартфон",       32000.0},
        {"Наушники",        4500.0},
        {"Клавиатура",      2300.0},
        {"Мышь",            1200.0},
        {"Монитор",        18000.0},
        {"Принтер",         9800.0},
        {"Веб-камера",      3400.0},
        {"Коврик для мыши",  700.0},
        {"USB-хаб",         1900.0},
    };
    FILE *f = fopen(PRODUCTS_FILE, "w");
    if (!f) { log_event("ERROR", "Не удалось создать %s: %s", PRODUCTS_FILE, strerror(errno)); return; }
    fprintf(f, "# ID;Название;Цена\n");
    for (size_t i = 0; i < sizeof(demo)/sizeof(demo[0]); ++i)
        fprintf(f, "%d;%s;%.2f\n", (int)i + 1, demo[i].name, demo[i].price);
    fclose(f);
    log_event("INFO", "Сгенерирован тестовый файл товаров %s (%zu шт.)",
              PRODUCTS_FILE, sizeof(demo)/sizeof(demo[0]));
}

static void generate_default_customers(void)
{
    static const char *names[] = {
        "Иванов И.И.", "Петров П.П.", "Сидорова А.В.", "Козлов Д.М.",
        "Новова Е.С.", "Морозов А.А.", "Волкова Н.И.", "Зайцев П.Р.",
        "Белова М.Ю.", "Громов К.С.", "Лисин В.В.", "Медведева О.Д."
    };
    FILE *f = fopen(CUSTOMERS_FILE, "w");
    if (!f) { log_event("ERROR", "Не удалось создать %s", CUSTOMERS_FILE); return; }
    fprintf(f, "# ID;Имя\n");
    for (size_t i = 0; i < sizeof(names)/sizeof(names[0]); ++i)
        fprintf(f, "%d;%s\n", (int)i + 1, names[i]);
    fclose(f);
    log_event("INFO", "Сгенерирован тестовый файл покупателей %s (%zu шт.)",
              CUSTOMERS_FILE, sizeof(names)/sizeof(names[0]));
}

/* ------------- Загрузка справочников (или создание тестовых) ------------ */
void load_or_create_databases(product_db_t *pd, customer_db_t *cd)
{
    memset(pd, 0, sizeof(*pd));
    memset(cd, 0, sizeof(*cd));

    /* Товары */
    if (access(PRODUCTS_FILE, F_OK) != 0) generate_default_products();
    FILE *f = fopen(PRODUCTS_FILE, "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f) && pd->count < MAX_PRODUCTS) {
            if (line[0] == '#' || line[0] == '\n') continue;   /* пропускаем комментарии */
            product_t p;
            char name[64];
            /* Формат: ID;Название;Цена */
            if (sscanf(line, "%d;%63[^;];%lf", &p.id, name, &p.price) == 3) {
                strncpy(p.name, name, sizeof(p.name)-1);
                p.name[sizeof(p.name)-1] = '\0';
                pd->items[pd->count++] = p;
            }
        }
        fclose(f);
        log_event("INFO", "Загружено товаров: %d", pd->count);
    } else {
        log_event("ERROR", "Не открывается %s", PRODUCTS_FILE);
    }

    /* Покупатели */
    if (access(CUSTOMERS_FILE, F_OK) != 0) generate_default_customers();
    f = fopen(CUSTOMERS_FILE, "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f) && cd->count < MAX_CUSTOMERS) {
            if (line[0] == '#' || line[0] == '\n') continue;
            customer_t c;
            char name[64];
            if (sscanf(line, "%d;%63[^\n]", &c.id, name) == 2) {
                strncpy(c.name, name, sizeof(c.name)-1);
                c.name[sizeof(c.name)-1] = '\0';
                cd->items[cd->count++] = c;
            }
        }
        fclose(f);
        log_event("INFO", "Загружено покупателей: %d", cd->count);
    } else {
        log_event("ERROR", "Не открывается %s", CUSTOMERS_FILE);
    }
}

/* ------------------ Чтение всех продаж из файла ----------------------
 * ВАЖНО: функция должна вызываться с уже зажатым g_file_mutex,
 * чтобы не получить «рваный» файл во время записи из потока симуляции.
 * Возвращает количество прочитанных записей или -1 при ошибке.          */
int load_sales(sales_db_t *sd)
{
    sd->count = 0;
    FILE *f = fopen(SALES_FILE, "r");
    if (!f) {
        /* Файла ещё нет — это нормально на старте, просто пустая база */
        return 0;
    }
    char line[256];
    while (fgets(line, sizeof(line), f) && sd->count < MAX_SALES) {
        if (line[0] == '#' || line[0] == '\n') continue;
        sale_t s;
        /* Формат: ID_продажи;YYYY-MM-DD HH:MM:SS;ID_покупателя;ID_товара */
        char datepart[32];
        int n = sscanf(line, "%ld;%31[^;];%d;%d",
                       &s.sale_id, datepart, &s.customer_id, &s.product_id);
        if (n != 4) continue;
        /* Разбираем дату-время в time_t через struct tm */
        struct tm tm_buf;
        memset(&tm_buf, 0, sizeof(tm_buf));
        if (sscanf(datepart, "%d-%d-%d %d:%d:%d",
                   &tm_buf.tm_year, &tm_buf.tm_mon, &tm_buf.tm_mday,
                   &tm_buf.tm_hour, &tm_buf.tm_min, &tm_buf.tm_sec) == 6) {
            tm_buf.tm_year -= 1900;
            tm_buf.tm_mon  -= 1;
            tm_buf.tm_isdst = -1;
            s.timestamp = mktime(&tm_buf);
        } else {
            s.timestamp = time(NULL);
        }
        sd->items[sd->count++] = s;
    }
    fclose(f);
    return sd->count;
}
