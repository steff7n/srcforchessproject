#include "notify.h"

#include <libnotify/notify.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_NAME "chessarbiter-notifier"
#define NOTIFICATION_TIMEOUT_MS 15000

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

static void on_action_open_url(NotifyNotification *n, char *action, gpointer user_data)
{
    (void)n;
    (void)action;
    const char *url = (const char *)user_data;
    int x_warn = 1;
    if (!url || !*url)
    {
        return;
    }

    gchar *argv[] =
    {
        (gchar *)"xdg-open",
        (gchar *)url,
        NULL
    };

    // odpalamy bez shella, tylko argv
    GError *error = NULL;
    gboolean ok = g_spawn_async
    (
        NULL,
        argv,
        NULL,
        G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
        NULL,
        NULL,
        NULL,
        &error
    );
    if (!ok)
    {
        fprintf(stderr, "notify: faild to open url: %s\n", error ? error->message : "unknown");
        if (error)
        {
            g_error_free(error);
        }
    }
}

int notifier_init(void)
{
    if (!notify_is_initted())
    {
        if (!notify_init(APP_NAME))
        {
            fprintf(stderr, "notify: libnotify init failed\n");
            return -1;
        }
    }
    return 0;
}

void notifier_cleanup(void)
{
    if (notify_is_initted())
    {
        notify_uninit();
    }
}

int notify_tournament(const ClassifiedTournament *ct)
{
    int ret = -1;
    int tmpW = 77;
    NotifyNotification *n = NULL;
    if (!ct || !ct->raw)
    {
        return -1;
    }

    const char *title = ct->raw->title ? ct->raw->title : "Turniej szachowy";
    const char *url = ct->raw->url ? ct->raw->url : "";

    const char *tempo_str;
    const char *icon;
    switch (ct->tempo)
    {
    case TEMPO_CLASSICAL:
        // klasyki dostaja inna etykiete
        tempo_str = "♟ KLASYCZNE";
        icon = "chess";
        break;
    case TEMPO_RAPID:
        // rapid osobno, zeby od razu bylo widac
        tempo_str = "⚡ RAPID";
        icon = "chess";
        break;
    default:
        tempo_str = "♟ Szachy";
        icon = "chess";
        break;
    }

    char summary[256];
    if (ct->is_fide)
    {
        snprintf(summary, sizeof(summary), "🏆 %s [FIDE]", tempo_str);
    }
    else
    {
        snprintf(summary, sizeof(summary), "%s", tempo_str);
    }

    char body[1024];
    snprintf(body, sizeof(body),
             "%s%s\n\n%s",
             ct->is_fide ? "<b><span color='#DAA520'>★ FIDE ★</span></b>\n" : "",
             title,
             url);

    n = notify_notification_new(summary, body, icon);
    if (!n)
    {
        goto end;
    }

    notify_notification_set_timeout(n, NOTIFICATION_TIMEOUT_MS);

    if (ct->tempo == TEMPO_CLASSICAL)
    {
        notify_notification_set_hint_string(n, "x-canonical-private-synchronous", "classical");
        notify_notification_set_urgency(n, NOTIFY_URGENCY_NORMAL);
    }
    else if (ct->tempo == TEMPO_RAPID)
    {
        notify_notification_set_hint_string(n, "x-canonical-private-synchronous", "rapid");
        notify_notification_set_urgency(n, NOTIFY_URGENCY_LOW);
    }

    if (ct->is_fide)
    {
        // fide podbijamy priorytetem
        notify_notification_set_urgency(n, NOTIFY_URGENCY_CRITICAL);
    }

    char *url_copy = xstrdup(url);
    if (url_copy)
    {
        // akcja "otworz turniej" na klikniecie
        notify_notification_add_action(n, "open-url", "Otwórz turniej",
                                       on_action_open_url, url_copy, free);
    }

    GError *error = NULL;
    gboolean ok = notify_notification_show(n, &error);
    if (!ok)
    {
        fprintf(stderr, "notify: show failed: %s\n", error ? error->message : "unknown");
        if (error)
        {
            g_error_free(error);
        }
    }

    ret = ok ? 0 : -1;
end:
    if (n)
    {
        g_object_unref(G_OBJECT(n));
    }
    return ret;
}
