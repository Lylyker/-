/* sales_model.c — реализация модели данных и работы с файлами */
#include "sales_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SALES_FILE_HEADER "sale_id;datetime;customer_id;goods_id;qty;amount\n"

void fmt_time(time_t t, char *buf, size_t bufsz)
{
    struct tm tm_buf;
    localtime_r(&t, &tm_buf);
    strftime(buf, bufsz, "%Y-%m-%d %H:%M:%S", &tm_buf);
}

/* ---------- Справочники ---------- */

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r' ||
                       s[len-1] == ' '  || s[len-1] == '\t')) {
        s[--len] = '\0';
    }
    return s;
}

int goods_load(const char *path, goods_t *arr, int max_items)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[256];
    int n = 0, lineno = 0;
    while (fgets(line, sizeof(line), f) && n < max_items) {
        lineno++;
        char *p = trim(line);
        if (*p == '\0' || *p == '#') continue;          /* пустые/комментарии */

        /* пропускаем строку заголовка, если первая буква не цифра */
        if ((*p < '0' || *p > '9') && lineno == 1) continue;

        char name[MAX_NAME_LEN];
        goods_t g;
        int got = sscanf(p, "%d;%63[^;];%lf", &g.id, name, &g.price);
        if (got != 3) {
            /* допускаем вариант без цены (только id;name) — цена 0 */
            got = sscanf(p, "%d;%63[^;]", &g.id, name);
            if (got != 2) continue;
            g.price = 0.0;
        }
        strncpy(g.name, trim(name), MAX_NAME_LEN - 1);
        g.name[MAX_NAME_LEN - 1] = '\0';
        arr[n++] = g;
    }
    fclose(f);
    return n;
}

int customers_load(const char *path, customer_t *arr, int max_items)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[256];
    int n = 0, lineno = 0;
    while (fgets(line, sizeof(line), f) && n < max_items) {
        lineno++;
        char *p = trim(line);
        if (*p == '\0' || *p == '#') continue;
        if ((*p < '0' || *p > '9') && lineno == 1) continue;

        customer_t c;
        char name[MAX_NAME_LEN];
        if (sscanf(p, "%d;%63[^\n]", &c.id, name) != 2) continue;
        strncpy(c.name, trim(name), MAX_NAME_LEN - 1);
        c.name[MAX_NAME_LEN - 1] = '\0';
        arr[n++] = c;
    }
    fclose(f);
    return n;
}

const goods_t *goods_find(const goods_t *arr, int n, int id)
{
    for (int i = 0; i < n; i++)
        if (arr[i].id == id) return &arr[i];
    return NULL;
}

/* ---------- Список продаж ---------- */

void sales_list_init(sales_list_t *sl)
{
    sl->items = NULL;
    sl->count = 0;
    sl->capacity = 0;
    pthread_mutex_init(&sl->mtx, NULL);
    sl->next_sale_id = 1;
}

void sales_list_free(sales_list_t *sl)
{
    free(sl->items);
    sl->items = NULL;
    sl->count = sl->capacity = 0;
    pthread_mutex_destroy(&sl->mtx);
}

/* Внутренняя функция без блокировки — вызывается из-под мьютекса. */
static int sales_list_push_locked(sales_list_t *sl, const sale_t *s)
{
    if (sl->count == sl->capacity) {
        size_t newcap = sl->capacity ? sl->capacity * 2 : 256;
        sale_t *tmp = realloc(sl->items, newcap * sizeof(*tmp));
        if (!tmp) return -1;
        sl->items = tmp;
        sl->capacity = newcap;
    }
    sl->items[sl->count++] = *s;
    return 0;
}

/* Публичная версия: вызывать только удерживая sl->mtx. */
int sales_list_push(sales_list_t *sl, const sale_t *s)
{
    return sales_list_push_locked(sl, s);
}

int sales_list_add(sales_list_t *sl, time_t ts, int customer_id,
                   int goods_id, int qty, double amount)
{
    sale_t s = { .ts = ts, .customer_id = customer_id,
                 .goods_id = goods_id, .qty = qty, .amount = amount };
    pthread_mutex_lock(&sl->mtx);
    s.sale_id = sl->next_sale_id++;
    int rc = sales_list_push_locked(sl, &s);
    pthread_mutex_unlock(&sl->mtx);
    return rc;
}

sale_t *sales_list_snapshot(sales_list_t *sl, size_t *out_count)
{
    pthread_mutex_lock(&sl->mtx);
    size_t n = sl->count;
    sale_t *copy = NULL;
    if (n > 0) {
        copy = malloc(n * sizeof(*copy));
        if (copy) memcpy(copy, sl->items, n * sizeof(*copy));
    }
    pthread_mutex_unlock(&sl->mtx);
    *out_count = copy ? n : 0;
    return copy;
}

/* ---------- Файл продаж ---------- */

int sales_file_write_header(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fputs(SALES_FILE_HEADER, f);
    fclose(f);
    return 0;
}

int sales_file_append(const char *path, const sale_t *s)
{
    FILE *f = fopen(path, "a");
    if (!f) return -1;
    char tbuf[32];
    fmt_time(s->ts, tbuf, sizeof(tbuf));
    fprintf(f, "%ld;%s;%d;%d;%d;%.2f\n",
            s->sale_id, tbuf, s->customer_id, s->goods_id, s->qty, s->amount);
    fclose(f);
    return 0;
}

int sales_file_load(const char *path, sales_list_t *sl)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[256];
    int loaded = 0;
    long max_id = 0;
    while (fgets(line, sizeof(line), f)) {
        char *p = trim(line);
        if (*p == '\0' || *p == '#' || (*p < '0' || *p > '9')) continue;

        sale_t s;
        char tbuf[32];
        if (sscanf(p, "%ld;%31[^;];%d;%d;%d;%lf",
                   &s.sale_id, tbuf, &s.customer_id, &s.goods_id,
                   &s.qty, &s.amount) != 6) continue;

        /* разбор даты "YYYY-MM-DD HH:MM:SS" */
        struct tm tm_buf = {0};
        if (sscanf(tbuf, "%d-%d-%d %d:%d:%d",
                   &tm_buf.tm_year, &tm_buf.tm_mon, &tm_buf.tm_mday,
                   &tm_buf.tm_hour, &tm_buf.tm_min, &tm_buf.tm_sec) != 6) continue;
        tm_buf.tm_year -= 1900;
        tm_buf.tm_mon  -= 1;
        tm_buf.tm_isdst = -1;
        s.ts = mktime(&tm_buf);

        if (sales_list_push_locked(sl, &s) == 0) {
            loaded++;
            if (s.sale_id > max_id) max_id = s.sale_id;
        }
    }
    fclose(f);
    sl->next_sale_id = max_id + 1;
    return loaded;
}
