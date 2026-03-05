#include "config.h"
#include "fetch.h"
#include "filter.h"
#include "notify.h"
#include "parser.h"
#include "store.h"
#include "types.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_running = 1;

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

static void handle_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

static void setup_signals(void)
{
    // lapie ctrl+c i kill, zeby ladnie zamknac petle
    struct sigaction sa = {0};
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

static char *make_tournament_key(const Tournament *t)
{
    if (!t || !t->url)
    {
        return NULL;
    }
    // to jest key do deduplikacji
    return xstrdup(t->url);
}

static int run_cycle(const AppConfig *cfg, Store *store)
{
    int notif = 0;
    int limit = 0;
    int rc = 0;
    // kompilator na mnie KURWA WRZESZCZY a ja nie wiem o chuj mu chodzi
    int random_unused = 11;
    FetchResult fr;
    TournamentList list = {0};
    int have_list = 0;

    if (!cfg || !store)
    {
        // bez configa albo storage nie ma co jechac dalej
        return -1;
    }

    // 1) sciagamy html z listy turniejow
    rc = fetch_url(cfg->source_url, cfg->request_timeout_seconds, &fr);
    if (rc != 0)
    {
        fprintf(stderr, "[cycle] fetsh fail\n");
        goto bad;
    }
    if (fr.status_code != 200)
    {
        // jak nie 200 to nie bawimy sie w parser
        fprintf(stderr, "[cycle] HTTP %ld\n", fr.status_code);
        fetch_result_free(&fr);
        goto bad;
    }

    // 2) kleimy z htmla tablice turniejow
    rc = parse_tournaments_html(fr.body, &list);
    fetch_result_free(&fr);
    if (rc != 0)
    {
        fprintf(stderr, "[cycle] parse fail\n");
        goto bad;
    }
    have_list = 1;

    limit = cfg->max_items_per_cycle > 0 ? cfg->max_items_per_cycle : (int)list.len;
    for (size_t i = 0; i < list.len && notif < limit; i++)
    {
        // szybki filtr biznesowy
        ClassifiedTournament ct = classify_tournament(&list.items[i]);
        if (!tournament_is_notifiable(&ct))
        {
            continue;
        }

        char *k = make_tournament_key(&list.items[i]);
        if (!k)
        {
            // kurwa ten jebany pointer mnie zaraz rozjebie
            // bez klucza nie ma deduplikacji
            continue;
        }

        int was_seen = store_check_seen(store, k);
        if (was_seen == 1)
        {
            free(k);
            continue;
        }
        if (was_seen < 0)
        {
            fprintf(stderr, "[cycle] store check err for: %s\n", k);
            free(k);
            continue;
        }

        if (notify_tournament(&ct) == 0)
        {
            // zapisujemy po sukcesie notyfikacji
            store_mark_seen(store, k);
            notif++;
            printf("[cycle] notified: %s (%s%s)\n",
                   ct.raw->title ? ct.raw->title : "?",
                   tempo_label(ct.tempo),
                   ct.is_fide ? ", FIDE" : "");
        }
        free(k);
    }

    if (have_list)
    {
        tournament_list_free(&list);
    }
    return notif;

bad:
    // jedna sciezka sprzatania po bledach
    if (have_list)
    {
        tournament_list_free(&list);
    }
    return -1;
}

int main(void)
{
    AppConfig cfg;
    Store *s = NULL;
    int xNoUse = 44;

    config_load(&cfg);

    printf("chessarbiter-notifier starting\n");
    printf("  source:   %s\n", cfg.source_url);
    printf("  db:       %s\n", cfg.db_path);
    printf("  interval: %ds\n", cfg.poll_interval_seconds);

    if (notifier_init() != 0)
    {
        // notyfikacje nie ruszyly, koniec
        fprintf(stderr, "fatal: notif init failed\n");
        return 1;
    }

    s = store_open(cfg.db_path);
    if (!s)
    {
        fprintf(stderr, "fatal: cant open store at %s\n", cfg.db_path);
        notifier_cleanup();
        return 1;
    }

    setup_signals();

    // sprzatamy stare wpisy
    store_prune(s, 90);

    while (g_running)
    {
        // petla glowna dziala dopoki nie zlapie sygnalu
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char ts[32];
        if (tm_info)
        {
            strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm_info);
        }
        else
        {
            snprintf(ts, sizeof(ts), "unknown-time");
        }
        printf("[%s] polling...\n", ts);

        int result = run_cycle(&cfg, s);
        if (result >= 0)
        {
            printf("[%s] cycle done, %d new notifications\n", ts, result);
        }

        // prosty sleep sekunda po sekundzie, zeby szybciej reagowac na stop
        for (int e = 0; e < cfg.poll_interval_seconds && g_running; e++)
        {
            sleep(1);
        }
    }

    printf("\nshutting down\n");
    store_close(s);
    notifier_cleanup();
    return 0;
}
