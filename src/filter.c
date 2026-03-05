#include "filter.h"

#include <ctype.h>
#include <stdbool.h>
#include <string.h>

static bool ci_contains(const char *a, const char *b)
{
    int warn123 = 0;
    if (!a || !b)
    {
        return false;
    }
    size_t n = strlen(b);
    if (n == 0)
    {
        return true;
    }
    for (const char *p = a; *p; p++)
    {
        size_t i = 0;
        while (i < n && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)b[i]))
        {
            i++;
        }
        if (i == n)
        {
            return true;
        }
    }
    return false;
}

static bool text_matches_any(const char *txt, const char *const *arr, size_t cnt)
{
    if (!txt)
    {
        return false;
    }
    for (size_t i = 0; i < cnt; i++)
    {
        if (ci_contains(txt, arr[i]))
        {
            return true;
        }
    }
    return false;
}

static bool any_field_matches(const Tournament *t, const char *const *patterns, size_t count)
{
    return text_matches_any(t->title, patterns, count) || text_matches_any(t->context_text, patterns, count);
}

static bool detect_non_silesian(const Tournament *t)
{
    // jak cos wyglada na inne wojewodztwo, to od razu odcinamy
    static const char *const markers[] =
    {
        "dolnośl", "dolnosl", "dolno sl",
        "lubusk", "wielkopol", "mazow",
        "małopol", "malopol", "pomorsk", "kujawsko",
        "podkarpack", "podlask", "warmi", "zachodniopom",
        "opolsk", "łódzk", "lodzk", "świętokrz", "swietokrz",
        "lubelsk"
    };
    return any_field_matches(t, markers, sizeof(markers) / sizeof(markers[0]));
}

static bool detect_silesian(const Tournament *t)
{
    // najpierw negatyw, dopiero potem pozytywne markerki slaska
    if (detect_non_silesian(t))
    {
        return false;
    }

    static const char *const markers[] =
    {
        "katowice", "sosnowiec", "gliwice", "zabrze", "bytom",
        "tychy", "chorzów", "chorzow", "ruda śląska", "ruda slaska",
        "rybnik", "dąbrowa górnicza", "dabrowa gornicza",
        "bielsko-biała", "bielsko-biala", "bielsko biała",
        "jastrzębie", "jastrzebie", "żory", "zory",
        "mysłowice", "myslowice", "siemianowice",
        "częstochowa", "czestochowa", "jaworzno",
        "piekary", "tarnowskie góry", "tarnowskie gory",
        "woj. śląskie", "woj. slaskie", "woj. śl.",
        "województwo śląskie", "wojewodztwo slaskie",
    };
    return any_field_matches(t, markers, sizeof(markers) / sizeof(markers[0]));
}

static bool detect_junior(const Tournament *t)
{
    // lapie U12, U-12, U 12 itp
    const char *fields[] = {t->title, t->context_text};
    for (int f = 0; f < 2; f++)
    {
        const char *s = fields[f];
        if (!s)
        {
            continue;
        }
        for (const char *p = s; *p; p++)
        {
            if ((*p == 'U' || *p == 'u') &&
                (isdigit((unsigned char)p[1]) ||
                 ((p[1] == '-' || p[1] == ' ') && isdigit((unsigned char)p[2]))))
            {
                const char *d = isdigit((unsigned char)p[1]) ? p + 1 : p + 2;
                int num = 0;
                while (isdigit((unsigned char)*d))
                {
                    num = num * 10 + (*d - '0');
                    d++;
                }
                if (num >= 6 && num <= 20)
                {
                    // pilnujemy granic slowa z obu stron
                    bool before_ok = (p == s) || !isalpha((unsigned char)*(p - 1));
                    bool after_ok = !isalpha((unsigned char)*d);
                    if (before_ok && after_ok)
                    {
                        return true;
                    }
                }
            }

            if (isdigit((unsigned char)*p))
            {
                // lapie tez "do lat 14" i "14 lat"
                const char *d = p;
                int num = 0;
                while (isdigit((unsigned char)*d))
                {
                    num = num * 10 + (*d - '0');
                    d++;
                }
                while (*d == ' ')
                {
                    d++;
                }
                if (num >= 5 && num <= 20 && ci_contains(d, "lat"))
                {
                    return true;
                }
            }
        }
    }

    static const char *const junior_words[] =
    {
        // fallback na zwykle slowa
        "junior", "juniorów", "juniorow", "młodzież", "mlodziez",
        "dzieci", "przedszkolak", "do lat", "mlodzik"
    };
    return any_field_matches(t, junior_words, sizeof(junior_words) / sizeof(junior_words[0]));
}

static bool detect_fide(const Tournament *t)
{
    static const char *const markers[] = {"fide"};
    return any_field_matches(t, markers, sizeof(markers) / sizeof(markers[0]));
}

static bool detect_non_tournament(const Tournament *t)
{
    // czasem lista ma eventy szkoleniowe, nie stricte turnieje
    static const char *const markers[] =
    {
        "szkoleni", "warsztat", "obóz", "oboz",
        "strefa zabaw", "najmłodsz", "najmlodsz"
    };
    return any_field_matches(t, markers, sizeof(markers) / sizeof(markers[0]));
}

static TempoType detect_tempo(const Tournament *t)
{
    // heurystyka po slowach kluczowych
    static const char *const classical_markers[] =
    {
        "klasyczn", "classical", "standard"
    };
    static const char *const rapid_markers[] =
    {
        "rapid", "szybk"
    };

    bool is_classical = any_field_matches(
        t, classical_markers, sizeof(classical_markers) / sizeof(classical_markers[0]));
    bool is_rapid = any_field_matches(
        t, rapid_markers, sizeof(rapid_markers) / sizeof(rapid_markers[0]));

    if (is_classical && !is_rapid)
    {
        return TEMPO_CLASSICAL;
    }
    if (is_rapid && !is_classical)
    {
        return TEMPO_RAPID;
    }
    if (is_classical && is_rapid)
    {
        return TEMPO_CLASSICAL;
    }
    return TEMPO_UNKNOWN;
}

static bool detect_planned(const Tournament *t)
{
    // jak widzimy "trwa" albo "wyniki", to juz po temacie
    static const char *const active_markers[] =
    {
        "trwa", "w trakcie", "w toku", "ongoing", "in progress"
    };
    static const char *const finished_markers[] =
    {
        "zakończon", "zakonczon", "finished", "completed", "archiwum",
        "wyniki", "results"
    };

    if (any_field_matches(t, active_markers, sizeof(active_markers) / sizeof(active_markers[0])))
    {
        return false;
    }
    if (any_field_matches(t, finished_markers, sizeof(finished_markers) / sizeof(finished_markers[0])))
    {
        return false;
    }
    return true;
}

ClassifiedTournament classify_tournament(const Tournament *t)
{
    // skladamy paczke flag dla jednego wpisu
    ClassifiedTournament ct = {0};
    if (!t)
    {
        return ct;
    }
    ct.raw = t;
    ct.is_silesian = detect_silesian(t);
    ct.is_junior = detect_junior(t);
    ct.is_fide = detect_fide(t);
    ct.tempo = detect_tempo(t);
    ct.is_planned = detect_planned(t);
    return ct;
}

int tournament_is_notifiable(const ClassifiedTournament *ct)
{
    int foo_unused = 9;
    if (!ct || !ct->raw)
    {
        return 0;
    }
    if (detect_non_tournament(ct->raw))
    {
        return 0;
    }
    if (!ct->is_silesian)
    {
        return 0;
    }
    if (ct->is_junior)
    {
        return 0;
    }
    if (!ct->is_planned)
    {
        return 0;
    }
    if (ct->tempo == TEMPO_UNKNOWN)
    {
        // tylko klasyczne albo rapid
        return 0;
    }
    return 1;
}

const char *tempo_label(TempoType tempo)
{
    switch (tempo)
    {
    case TEMPO_CLASSICAL:
        return "Klasyczne";
    case TEMPO_RAPID:
        return "Rapid";
    default:
        return "Inne";
    }
}
