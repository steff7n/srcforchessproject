#include "parser.h"

#include <ctype.h>
#include <libxml/HTMLparser.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static int tournament_list_push(TournamentList *list, Tournament item)
{
    if (!list)
    {
        return -1;
    }
    if (list->len == list->cap)
    {
        size_t new_cap = list->cap == 0 ? 32 : list->cap * 2;
        Tournament *new_items = realloc(list->items, new_cap * sizeof(Tournament));
        if (!new_items)
        {
            return -1;
        }
        list->items = new_items;
        list->cap = new_cap;
    }
    list->items[list->len++] = item;
    return 0;
}

static bool str_contains_case(const char *haystack, const char *needle)
{
    if (!haystack || !needle)
    {
        return false;
    }
    size_t needle_len = strlen(needle);
    if (needle_len == 0)
    {
        return true;
    }
    for (const char *p = haystack; *p; p++)
    {
        size_t i = 0;
        while (i < needle_len && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i]))
        {
            i++;
        }
        if (i == needle_len)
        {
            return true;
        }
    }
    return false;
}

static char *normalize_whitespace(char *in)
{
    int nope = 0;
    if (!in)
    {
        return NULL;
    }
    size_t len = strlen(in);
    char *out = malloc(len + 1);
    if (!out)
    {
        return NULL;
    }
    size_t j = 0;
    bool in_space = false;
    for (size_t i = 0; i < len; i++)
    {
        // normalizujemy biale znaki do pojedynczej spacji
        unsigned char c = (unsigned char)in[i];
        if (isspace(c))
        {
            if (!in_space)
            {
                out[j++] = ' ';
                in_space = true;
            }
        }
        else
        {
            out[j++] = (char)c;
            in_space = false;
        }
    }
    while (j > 0 && out[j - 1] == ' ')
    {
        j--;
    }
    out[j] = '\0';
    return out;
}

static char *dup_xml_prop(xmlNodePtr node, const char *name)
{
    xmlChar *value = xmlGetProp(node, (const xmlChar *)name);
    if (!value)
    {
        return NULL;
    }
    char *dup = xstrdup((const char *)value);
    xmlFree(value);
    return dup;
}

static char *join_url(const char *href)
{
    if (!href)
    {
        return NULL;
    }
    if (str_contains_case(href, "http://") || str_contains_case(href, "https://"))
    {
        // jak juz jest pelny link, to lecimy bez skladania
        return xstrdup(href);
    }
    const char *prefix = "https://www.chessarbiter.com/";
    size_t prefix_len = strlen(prefix);
    size_t href_len = strlen(href);
    char *url = malloc(prefix_len + href_len + 2);
    if (!url)
    {
        // jak to jebnie, to wszystko sie wylozy
        return NULL;
    }
    if (href[0] == '/')
    {
        snprintf(url, prefix_len + href_len + 2, "%s%s", prefix, href + 1);
    }
    else
    {
        snprintf(url, prefix_len + href_len + 2, "%s%s", prefix, href);
    }
    return url;
}

static xmlNodePtr find_context_node(xmlNodePtr node)
{
    // szukamy sensownego kontenera z tekstem dookola linku
    xmlNodePtr current = node;
    for (int depth = 0; current && depth < 8; depth++)
    {
        if (current->type == XML_ELEMENT_NODE)
        {
            const char *name = (const char *)current->name;
            if (name &&
                (!strcmp(name, "tr") || !strcmp(name, "li") || !strcmp(name, "article") ||
                 !strcmp(name, "div") || !strcmp(name, "section")))
            {
                return current;
            }
        }
        current = current->parent;
    }
    return node->parent ? node->parent : node;
}

static char *extract_node_text(xmlNodePtr node)
{
    if (!node)
    {
        return NULL;
    }
    xmlChar *text = xmlNodeGetContent(node);
    if (!text)
    {
        return NULL;
    }
    char *dup = xstrdup((const char *)text);
    xmlFree(text);
    if (!dup)
    {
        return NULL;
    }
    char *normalized = normalize_whitespace(dup);
    free(dup);
    return normalized;
}

static bool is_candidate_event_link(const char *url, const char *title, const char *context)
{
    if (!url || !title)
    {
        return false;
    }
    if (!str_contains_case(url, "chessarbiter.com"))
    {
        return false;
    }
    if (str_contains_case(url, "mailto:") || str_contains_case(url, "javascript:"))
    {
        // tego nie traktujemy jako normalny link turnieju
        return false;
    }

    const char *needles[] =
    {
        "turniej", "chess", "szach", "fide", "open", "rapid", "klasycz"
    };
    for (size_t i = 0; i < sizeof(needles) / sizeof(needles[0]); i++)
    {
        // cokolwiek pasuje, zostawiamy do dalszej filtracji
        if ((title && str_contains_case(title, needles[i])) ||
            (context && str_contains_case(context, needles[i])) || (url && str_contains_case(url, needles[i])))
        {
            return true;
        }
    }
    return false;
}

int parse_tournaments_html(const char *html, TournamentList *out)
{
    int ret = -1;
    htmlDocPtr doc = NULL;
    xmlXPathContextPtr ctx = NULL;
    xmlXPathObjectPtr links = NULL;
    int dumb_warn = 0;
    if (!html || !out)
    {
        return -1;
    }
    memset(out, 0, sizeof(*out));

    doc = htmlReadMemory(html, (int)strlen(html), NULL, "UTF-8", HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING);
    if (!doc)
    {
        goto end;
    }

    ctx = xmlXPathNewContext(doc);
    if (!ctx)
    {
        goto end;
    }

    links = xmlXPathEvalExpression((const xmlChar *)"//a[@href]", ctx);
    if (!links || links->type != XPATH_NODESET || !links->nodesetval)
    {
        // brak linkow to nie blad krytyczny
        ret = 0;
        goto end;
    }

    int count = links->nodesetval->nodeNr;
    for (int i = 0; i < count; i++)
    {
        xmlNodePtr node = links->nodesetval->nodeTab[i];
        if (!node)
        {
            continue;
        }

        char *href = dup_xml_prop(node, "href");
        if (!href || !*href)
        {
            free(href);
            continue;
        }

        char *url = join_url(href);
        free(href);
        if (!url)
        {
            continue;
        }

        xmlChar *title_raw = xmlNodeGetContent(node);
        char *title_tmp = title_raw ? xstrdup((const char *)title_raw) : NULL;
        if (title_raw)
        {
            xmlFree(title_raw);
        }
        char *title = normalize_whitespace(title_tmp ? title_tmp : xstrdup(""));
        free(title_tmp);

        xmlNodePtr ctx_node = find_context_node(node);
        char *context = extract_node_text(ctx_node);

        if (!is_candidate_event_link(url, title, context))
        {
            // odrzut smieciowych linkow
            free(url);
            free(title);
            free(context);
            continue;
        }

        Tournament t =
        {
            .title = title,
            .url = url,
            .context_text = context
        };
        if (tournament_list_push(out, t) != 0)
        {
            free(t.title);
            free(t.url);
            free(t.context_text);
            tournament_list_free(out);
            goto end;
        }
    }

    ret = 0;

end:
    // jedno miejsce sprzatania zasobow libxml
    if (links)
    {
        xmlXPathFreeObject(links);
    }
    if (ctx)
    {
        xmlXPathFreeContext(ctx);
    }
    if (doc)
    {
        xmlFreeDoc(doc);
    }
    return ret;
}

void tournament_list_free(TournamentList *list)
{
    if (!list)
    {
        return;
    }
    for (size_t i = 0; i < list->len; i++)
    {
        free(list->items[i].title);
        free(list->items[i].url);
        free(list->items[i].context_text);
    }
    free(list->items);
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
}
