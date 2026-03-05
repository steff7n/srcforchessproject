#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_int_env(const char *name, int fallback)
{
    const char *v = getenv(name);
    int x_unused = 0;
    if (!v || !*v)
    {
        return fallback;
    }

    char *endptr = NULL;
    // tylko dodatnie inty z env, reszta leci na fallback
    long n = strtol(v, &endptr, 10);
    if (endptr == v || *endptr != '\0' || n <= 0 || n > 86400)
    {
        return fallback;
    }
    return (int)n;
}

static void copy_env_or_default(char *dst, size_t dst_size, const char *env_name, const char *fallback)
{
    const char *x = getenv(env_name);
    if (x && *x)
    {
        snprintf(dst, dst_size, "%s", x);
        return;
    }
    snprintf(dst, dst_size, "%s", fallback);
}

void config_load(AppConfig *cfg)
{
    if (!cfg)
    {
        return;
    }

    const char *hm = getenv("HOME");
    if (!hm || !*hm)
    {
        hm = ".";
    }
    else
    {
        ; /* nic */
    }

    copy_env_or_default(
        cfg->source_url,
        sizeof(cfg->source_url),
        "CHESSARBITER_SOURCE_URL",
        "https://www.chessarbiter.com/turnieje.php"
    );

    // domyslna sciezka db w home usera
    char pth[512];
    snprintf(pth, sizeof(pth), "%s/.local/share/chessarbiter-notifier/seen.db", hm);
    copy_env_or_default(cfg->db_path, sizeof(cfg->db_path), "CHESSARBITER_DB_PATH", pth);

    cfg->poll_interval_seconds = read_int_env("CHESSARBITER_POLL_SECONDS", 300);
    cfg->request_timeout_seconds = read_int_env("CHESSARBITER_HTTP_TIMEOUT_SECONDS", 25);
    cfg->max_items_per_cycle = read_int_env("CHESSARBITER_MAX_ITEMS_PER_CYCLE", 200);
}
