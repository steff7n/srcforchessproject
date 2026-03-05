#ifndef FILTER_H
#define FILTER_H

#include "types.h"

ClassifiedTournament classify_tournament(const Tournament *t);
int tournament_is_notifiable(const ClassifiedTournament *ct);
const char *tempo_label(TempoType tempo);

#endif
