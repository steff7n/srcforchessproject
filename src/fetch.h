#ifndef FETCH_H
#define FETCH_H

typedef struct FetchResult
{
    char *body;
    long status_code;
} FetchResult;

int fetch_url(const char *url, int timeout_seconds, FetchResult *out);
void fetch_result_free(FetchResult *result);

#endif
