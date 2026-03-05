#ifndef STORE_H
#define STORE_H

typedef struct Store Store;

Store *store_open(const char *db_path);
void store_close(Store *store);

/* Zwraca 1 gdy wpis juz byl, 0 gdy nowy, -1 przy bledzie. */
int store_check_seen(Store *store, const char *tournament_key);

/* Oznacza wpis jako widziany. Zwraca 0 gdy OK. */
int store_mark_seen(Store *store, const char *tournament_key);

/* Usuwa stare wpisy starsze niz max_age_days. Liczba usunietych albo -1. */
int store_prune(Store *store, int max_age_days);

#endif
