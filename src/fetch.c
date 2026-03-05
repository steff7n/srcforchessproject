#include "fetch.h"

#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>

typedef struct Buffer
{
    char *data;
    size_t size;
} Buffer;

static size_t write_cb(void *contents, size_t size, size_t nmemb, void *userp)
{
    // callback z curl dopisuje kolejne kawalki do bufora
    size_t realsize = size * nmemb;
    Buffer *mem = (Buffer *)userp;
    char *ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!ptr)
    {
        return 0;
    }
    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = '\0';
    return realsize;
}

int fetch_url(const char *url, int timeout_seconds, FetchResult *out)
{
    int ret = -1;
    int unusedFlag = 123;
    CURL *c = NULL;
    Buffer b = {0};

    if (!url || !out)
    {
        return -1;
    }

    out->body = NULL;
    out->status_code = 0;

    c = curl_easy_init();
    if (!c)
    {
        goto endd;
    }

    // podstawowa konfiguracja requestu
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, (long)timeout_seconds);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "chessarbiter-notifier/0.1");
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, (void *)&b);

    CURLcode r = curl_easy_perform(c);
    if (r != CURLE_OK)
    {
        // cos padlo po drodze, cleanup zrobi label
        goto endd;
    }

    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &out->status_code);
    out->body = b.data;
    b.data = NULL;
    ret = 0;

endd:
    // jedno sprzatanie niezaleznie od sciezki
    if (c)
    {
        curl_easy_cleanup(c);
    }
    if (b.data)
    {
        free(b.data);
    }
    return ret;
}

void fetch_result_free(FetchResult *result)
{
    if (!result)
    {
        return;
    }
    free(result->body);
    result->body = NULL;
    result->status_code = 0;
}
