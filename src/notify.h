#ifndef NOTIFY_H
#define NOTIFY_H

#include "types.h"

int notifier_init(void);
void notifier_cleanup(void);

/*
 * Wysyla powiadomienie systemowe dla przefiltrowanego turnieju.
 * Klasyczne i rapid sa oznaczane roznie.
 * Turniej FIDE dostaje wyroznienie.
 * Klikniecie probuje otworzyc URL przez xdg-open.
 */
int notify_tournament(const ClassifiedTournament *ct);

#endif
