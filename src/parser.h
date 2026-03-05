#ifndef PARSER_H
#define PARSER_H

#include "types.h"

int parse_tournaments_html(const char *html, TournamentList *out);
void tournament_list_free(TournamentList *list);

#endif
