#include "parsednx.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define DNX_LINE_MAX   4096
#define DNX_FIELD_MAX  256
#define DNX_ITEMS_MAX  128

static char scalar_buf[DNX_LINE_MAX];

static char *dnx_strdup(const char *s)
{
    size_t n;
    char *p;
    if (!s) return NULL;
    n = strlen(s) + 1;
    p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static char *
trim(char *s)
{
    char *e;

    while (*s && isspace((unsigned char)*s))
        s++;
    if (!*s)
        return s;

    e = s + strlen(s) - 1;
    while (e >= s && isspace((unsigned char)*e))
        *e-- = '\0';
    return s;
}

static void
strip_inline_comment(char *s)
{
    int quote = 0;
    char q = 0;

    for (; *s; s++) {
        if (quote) {
            if (*s == q && (s == s - 1 || s[-1] != '\\'))
                quote = 0;
        } else if (*s == '"' || *s == '\'') {
            quote = 1;
            q = *s;
        } else if (*s == '#') {
            *s = '\0';
            return;
        }
    }
}

static char *
unquote(char *s)
{
    size_t n;

    s = trim(s);
    n = strlen(s);
    if (n >= 2 &&
        ((s[0] == '"' && s[n - 1] == '"') ||
         (s[0] == '\'' && s[n - 1] == '\''))) {
        s[n - 1] = '\0';
        s++;
        /* Minimal C-style escaping; enough for paths and UTF-8 strings. */
        {
            char *r = s, *w = s;
            while (*r) {
                if (*r == '\\' && r[1]) {
                    r++;
                    switch (*r) {
                    case 'n': *w++ = '\n'; break;
                    case 't': *w++ = '\t'; break;
                    case 'r': *w++ = '\r'; break;
                    case '\\': *w++ = '\\'; break;
                    case '"': *w++ = '"'; break;
                    case '\'': *w++ = '\''; break;
                    default: *w++ = *r; break;
                    }
                    r++;
                } else {
                    *w++ = *r++;
                }
            }
            *w = '\0';
        }
    }
    return s;
}

static int
split_fields(char *s, char **out, size_t max)
{
    size_t n = 0;
    int quote = 0;
    char q = 0;
    char *start = s;

    for (; *s; s++) {
        if (quote) {
            if (*s == q && (s == start || s[-1] != '\\'))
                quote = 0;
        } else if (*s == '"' || *s == '\'') {
            quote = 1;
            q = *s;
        } else if (*s == ',') {
            if (n < max)
                out[n++] = trim(start);
            *s = '\0';
            start = s + 1;
        }
    }

    if (*trim(start) || n)
        if (n < max)
            out[n++] = trim(start);

    for (size_t i = 0; i < n; i++)
        out[i] = unquote(out[i]);

    return (int)n;
}

static int
parse_file(const char *filename, int source, DnxEntryCallback cb, void *ud)
{
    FILE *f;
    char line[DNX_LINE_MAX];
    char expanded[PATH_MAX];

    if (!filename)
        return 0;
    if (filename[0] == '~' && filename[1] == '/') {
        const char *home = getenv("HOME");
        if (!home)
            return 0;
        snprintf(expanded, sizeof(expanded), "%s/%s", home, filename + 2);
        filename = expanded;
    }
    char section[DNX_FIELD_MAX] = "";
    char listname[DNX_FIELD_MAX] = "";
    char table_name[DNX_FIELD_MAX] = "";
    char *items[DNX_ITEMS_MAX];

    f = fopen(filename, "r");
    if (!f)
        return 0; /* A missing config is not an error. */

    while (fgets(line, sizeof(line), f)) {
        char *p = trim(line);
        DnxEntry e;

        if (!*p)
            continue;
        strip_inline_comment(p);
        p = trim(p);
        if (!*p)
            continue;

        if (p[0] == ':' && strlen(p) > 1) {
            snprintf(section, sizeof(section), "%s", trim(p + 1));
            listname[0] = table_name[0] = '\0';
            continue;
        }

        if (p[0] == '*' && strlen(p) > 1) {
            snprintf(listname, sizeof(listname), "%s", trim(p + 1));
            table_name[0] = '\0';
            continue;
        }

        if (p[0] == '%' && strlen(p) > 1) {
            snprintf(table_name, sizeof(table_name), "%s", trim(p + 1));
            listname[0] = '\0';
            continue;
        }

        /* INI compatibility. */
        if (p[0] == '[') {
            char *close = strrchr(p, ']');
            if (close) {
                *close = '\0';
                snprintf(section, sizeof(section), "%s", trim(p + 1));
                listname[0] = table_name[0] = '\0';
                continue;
            }
        }

        memset(&e, 0, sizeof(e));
        e.section = section;
        e.source = source;

        if (listname[0]) {
            char *v = unquote(p);
            items[0] = v;
            e.type = DNX_LIST;
            e.key = listname;
            e.items = (const char *const *)items;
            e.count = 1;
            if (cb(&e, ud) < 0)
                break;
            continue;
        }

        if (table_name[0]) {
            int n = split_fields(p, items, DNX_ITEMS_MAX);
            if (n <= 0)
                continue;
            e.type = DNX_TABLE;
            e.key = table_name;
            e.items = (const char *const *)items;
            e.count = (size_t)n;
            if (cb(&e, ud) < 0)
                break;
            continue;
        }

        /* DNX: key: value. INI: key=value. */
        {
            char *sep = strchr(p, ':');
            char *eq = strchr(p, '=');
            if (eq && (!sep || eq < sep))
                sep = eq;

            if (sep) {
                *sep = '\0';
                e.type = DNX_SCALAR;
                e.key = trim(p);
                e.value = unquote(trim(sep + 1));
                if (cb(&e, ud) < 0)
                    break;
            }
        }
    }

    fclose(f);
    return 1;
}

int
dnx_foreach_ff(const char *default_file, const char *user_file,
               DnxEntryCallback callback, void *userdata)
{
    int ok = 0;

    if (default_file)
        ok |= parse_file(default_file, 0, callback, userdata);
    if (user_file)
        ok |= parse_file(user_file, 1, callback, userdata);

    return ok;
}

struct find_ctx {
    const char *path;
    char *value;
    size_t value_size;
    int found;
};

static int
find_cb(const DnxEntry *e, void *userdata)
{
    struct find_ctx *c = userdata;
    char path[DNX_FIELD_MAX * 2];

    if (e->type != DNX_SCALAR)
        return 0;

    if (e->section && *e->section)
        snprintf(path, sizeof(path), "%s.%s", e->section, e->key);
    else
        snprintf(path, sizeof(path), "%s", e->key);

    if (!strcasecmp(path, c->path)) {
        snprintf(c->value, c->value_size, "%s", e->value ? e->value : "");
        c->found = 1;
    }
    return 0;
}

char *
dnx_parseff(const char *default_file, const char *user_file, const char *path)
{
    struct find_ctx c = {
        .path = path,
        .value = scalar_buf,
        .value_size = sizeof(scalar_buf),
        .found = 0
    };

    scalar_buf[0] = '\0';
    dnx_foreach_ff(default_file, user_file, find_cb, &c);
    return c.found ? scalar_buf : NULL;
}

char *
dnx_parseon(const char *filename, const char *path)
{
    return dnx_parseff(filename, NULL, path);
}

const char *
dnx_get_str_ff(const char *def_file, const char *usr_file,
               const char *path, const char *fallback)
{
    char *v = dnx_parseff(def_file, usr_file, path);
    return (v && *v) ? v : fallback;
}

char *
dnx_get_strdup_ff(const char *def_file, const char *usr_file,
                  const char *path, const char *fallback)
{
    const char *v = dnx_get_str_ff(def_file, usr_file, path, fallback);
    return v ? dnx_strdup(v) : NULL;
}

static long long
parse_ll(const char *v, long long fallback)
{
    char *end;
    long long n;

    if (!v || !*v)
        return fallback;
    errno = 0;
    n = strtoll(v, &end, 0);
    if (errno || end == v)
        return fallback;
    while (isspace((unsigned char)*end))
        end++;
    return *end ? fallback : n;
}

static double
parse_double(const char *v, double fallback)
{
    char *end;
    double n;

    if (!v || !*v)
        return fallback;
    errno = 0;
    n = strtod(v, &end);
    if (errno || end == v)
        return fallback;
    while (isspace((unsigned char)*end))
        end++;
    if (*end == 'f' || *end == 'F')
        end++;
    while (isspace((unsigned char)*end))
        end++;
    return *end ? fallback : n;
}

int
dnx_get_int_ff(const char *d, const char *u, const char *p, int fallback)
{
    char *v = dnx_parseff(d, u, p);
    return v ? (int)parse_ll(v, fallback) : fallback;
}

unsigned int
dnx_get_uint_ff(const char *d, const char *u, const char *p,
                unsigned int fallback)
{
    char *v = dnx_parseff(d, u, p);
    return v ? (unsigned int)parse_ll(v, fallback) : fallback;
}

float
dnx_get_float_ff(const char *d, const char *u, const char *p, float fallback)
{
    char *v = dnx_parseff(d, u, p);
    return v ? (float)parse_double(v, fallback) : fallback;
}

double
dnx_get_double_ff(const char *d, const char *u, const char *p, double fallback)
{
    char *v = dnx_parseff(d, u, p);
    return v ? parse_double(v, fallback) : fallback;
}

bool
dnx_get_bool_ff(const char *d, const char *u, const char *p, bool fallback)
{
    char *v = dnx_parseff(d, u, p);
    if (!v)
        return fallback;
    if (!strcasecmp(v, "1") || !strcasecmp(v, "true") ||
        !strcasecmp(v, "yes") || !strcasecmp(v, "on"))
        return true;
    if (!strcasecmp(v, "0") || !strcasecmp(v, "false") ||
        !strcasecmp(v, "no") || !strcasecmp(v, "off"))
        return false;
    return fallback;
}
