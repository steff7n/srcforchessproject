#ifndef TYPES_H
#define TYPES_H

#include <stdbool.h>
#include <stddef.h>

typedef enum TempoType
{
    TEMPO_UNKNOWN = 0,
    TEMPO_CLASSICAL = 1,
    TEMPO_RAPID = 2
} TempoType;

typedef struct Tournament
{
    char *title;
    char *url;
    char *context_text;
} Tournament;

typedef struct TournamentList
{
    Tournament *items;
    size_t len;
    size_t cap;
} TournamentList;

typedef struct ClassifiedTournament
{
    const Tournament *raw;
    TempoType tempo;
    bool is_silesian;
    bool is_junior;
    bool is_fide;
    bool is_planned;
} ClassifiedTournament;

#endif
