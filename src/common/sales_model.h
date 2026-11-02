/* sales_model.h — модель данных проекта: товары, покупатели, продажи.
 * Общий модуль для симулятора продаж и анализатора. */
#ifndef SALES_MODEL_H
#define SALES_MODEL_H

#include <stddef.h>
#include <time.h>
#include <pthread.h>

#define MAX_NAME_LEN   64
#define MAX_GOODS      128
#define MAX_CUSTOMERS  512

/* Товар */
typedef struct {
    int   id;                     /* код товара */
    char  name[MAX_NAME_LEN];     /* наименование */
    double price;                 /* цена за единицу */
} goods_t;

/* Покупатель */
typedef struct {
    int   id;                     /* код покупателя */
    char  name[MAX_NAME_LEN];     /* ФИО / название */
} customer_t;

/* Запись о продаже */
typedef struct {
    long   sale_id;               /* ID продажи */
    time_t ts;                    /* дата и время продажи */
    int    customer_id;           /* код покупателя */
    int    goods_id;              /* код товара */
    int    qty;                   /* количество единиц */
    double amount;                /* сумма = цена * кол-во */
} sale_t;

/* Динамический массив продаж с мьютексом (потокобезопасное добавление/чтение). */
typedef struct {
    sale_t        *items;
    size_t         count;
    size_t         capacity;
    pthread_mutex_t mtx;
    long           next_sale_id;  /* генератор уникальных ID продаж */
} sales_list_t;

/* --- Справочники --- */
/* Загрузка товаров из CSV-файла: "id,name,price". Возвращает число записей или -1. */
int goods_load(const char *path, goods_t *arr, int max_items);
/* Загрузка покупателей из CSV-файла: "id,name". Возвращает число записей или -1. */
int customers_load(const char *path, customer_t *arr, int max_items);
const goods_t *goods_find(const goods_t *arr, int n, int id);

/* --- Список продаж --- */
void sales_list_init(sales_list_t *sl);
void sales_list_free(sales_list_t *sl);
/* Потокобезопасно добавить продажу (amount вычисляется вызывающим кодом). */
int sales_list_add(sales_list_t *sl, time_t ts, int customer_id,
                   int goods_id, int qty, double amount);
/* Добавить уже сформированную запись (с sale_id) БЕЗ блокировки —
 * вызывать только удерживая sl->mtx (нужно симулятору для атомарности
 * «ID + список + файл»). */
int sales_list_push(sales_list_t *sl, const sale_t *s);
/* Потокобезопасно скопировать все продажи в выделенный буфер.
 * Возвращает число копий или NULL при ошибке; *out_count задаёт размер. */
sale_t *sales_list_snapshot(sales_list_t *sl, size_t *out_count);

/* --- Файл продаж --- */
/* Формат строки: sale_id;ISO8601 datetime;customer_id;goods_id;qty;amount */
int sales_file_write_header(const char *path);
/* Аппендинг одной записи в файл продаж (для симулятора). */
int sales_file_append(const char *path, const sale_t *s);
/* Полная загрузка файла продаж в список (для анализатора). */
int sales_file_load(const char *path, sales_list_t *sl);

/* Утилита: time_t -> строка "YYYY-MM-DD HH:MM:SS" */
void fmt_time(time_t t, char *buf, size_t bufsz);

#endif /* SALES_MODEL_H */
