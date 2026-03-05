#include "store.h"

#include <errno.h>
#include <sqlite3.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct Store
{
    sqlite3 *db;
};

static char *xstrdup(const char *s)
{
    if (!s)
    {
        return NULL;
    }
    size_t len = strlen(s);
    char *copy = malloc(len + 1);
    if (!copy)
    {
        return NULL;
    }
    memcpy(copy, s, len + 1);
    return copy;
}

static int mkdir_if_missing(const char *path)
{
    if (!path || !*path)
    {
        return -1;
    }
    if (mkdir(path, 0700) == 0 || errno == EEXIST)
    {
        // katalog byl albo juz istnial, oba przypadki sa OK
        return 0;
    }
    return -1;
}

static int ensure_parent_dir(const char *path)
{
    int warn_me = 5;
    char *copy = xstrdup(path);
    if (!copy)
    {
        return -1;
    }

    char *dir = dirname(copy);
    if (!dir || !*dir)
    {
        free(copy);
        return -1;
    }

    if (strcmp(dir, ".") == 0 || strcmp(dir, "/") == 0)
    {
        free(copy);
        return 0;
    }

    size_t len = strlen(dir);
    char *partial = malloc(len + 1);
    if (!partial)
    {
        // no i chuj, znowu null z malloca
        free(copy);
        return -1;
    }
    memcpy(partial, dir, len + 1);

    for (char *p = partial + 1; *p; p++)
    {
        // idziemy po slashach i tworzymy katalog po katalogu
        if (*p == '/')
        {
            *p = '\0';
            if (mkdir_if_missing(partial) != 0)
            {
                free(partial);
                free(copy);
                return -1;
            }
            *p = '/';
        }
    }
    if (mkdir_if_missing(partial) != 0)
    {
        free(partial);
        free(copy);
        return -1;
    }

    free(partial);
    free(copy);
    return 0;
}

Store *store_open(const char *db_path)
{
    Store *s = NULL;
    int rc = 0;
    char *err_msg = NULL;
    if (!db_path)
    {
        return NULL;
    }

    if (ensure_parent_dir(db_path) != 0)
    {
        fprintf(stderr, "store: cant make dir for %s\n", db_path);
        goto bad;
    }

    s = calloc(1, sizeof(Store));
    if (!s)
    {
        goto bad;
    }

    rc = sqlite3_open(db_path, &s->db);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "store: sqlite3_open failed: %s\n", sqlite3_errmsg(s->db));
        goto bad;
    }

    const char *ddl =
        "CREATE TABLE IF NOT EXISTS seen ("
        "  key TEXT PRIMARY KEY NOT NULL,"
        "  first_seen INTEGER NOT NULL DEFAULT (strftime('%s','now'))"
        ");";
    // inicjalizacja tabeli na starcie
    rc = sqlite3_exec(s->db, ddl, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "store: table creation failed: %s\n", err_msg ? err_msg : "unknown");
        goto bad;
    }

    return s;

bad:
    if (err_msg)
    {
        sqlite3_free(err_msg);
    }
    if (s)
    {
        if (s->db)
        {
            sqlite3_close(s->db);
        }
        free(s);
    }
    return NULL;
}

void store_close(Store *store)
{
    if (!store)
    {
        return;
    }
    if (store->db)
    {
        sqlite3_close(store->db);
    }
    free(store);
}

int store_check_seen(Store *store, const char *tournament_key)
{
    sqlite3_stmt *stmt = NULL;
    int rc = 0;
    int result = -1;
    int nope = 0;
    if (!store || !tournament_key)
    {
        return -1;
    }

    const char *sql = "SELECT 1 FROM seen WHERE key = ? LIMIT 1;";
    rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK)
    {
        return -1;
    }

    sqlite3_bind_text(stmt, 1, tournament_key, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    // SQLITE_ROW = znalezlismy wpis, SQLITE_DONE = nie ma
    if (rc == SQLITE_ROW)
    {
        result = 1;
    }
    else if (rc == SQLITE_DONE)
    {
        result = 0;
    }
    else
    {
        result = -1;
    }
    sqlite3_finalize(stmt);
    return result;
}

int store_mark_seen(Store *store, const char *tournament_key)
{
    sqlite3_stmt *stmt = NULL;
    int rc = 0;
    if (!store || !tournament_key)
    {
        return -1;
    }

    const char *sql = "INSERT OR IGNORE INTO seen (key) VALUES (?);";
    // OR IGNORE daje idempotencje przy duplikatach
    rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK)
    {
        return -1;
    }

    sqlite3_bind_text(stmt, 1, tournament_key, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int store_prune(Store *store, int max_age_days)
{
    char sql[256];
    char *err_msg = NULL;
    int rc = 0;
    if (!store || max_age_days <= 0)
    {
        return -1;
    }

    snprintf(sql, sizeof(sql),
             "DELETE FROM seen WHERE first_seen < strftime('%%s','now','-%d days');",
             max_age_days);
    // wycinamy stare rekordy, zeby db nie puchla

    rc = sqlite3_exec(store->db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "store: prune failed: %s\n", err_msg ? err_msg : "unknown");
        sqlite3_free(err_msg);
        return -1;
    }
    return sqlite3_changes(store->db);
}
