#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>

typedef struct AppConfig
{
    char source_url[512];
    char db_path[512];
    int poll_interval_seconds;
    int request_timeout_seconds;
    int max_items_per_cycle;
} AppConfig;

void config_load(AppConfig *cfg);

#endif
