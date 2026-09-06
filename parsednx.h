#ifndef PARSEDNX_H
#define PARSEDNX_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DNX_SCALAR = 0,
    DNX_LIST,
    DNX_TABLE
} DnxEntryType;

typedef struct {
    DnxEntryType type;
    const char *section;
    const char *key;          /* only for DNX_SCALAR */
    const char *value;        /* scalar value */
    const char *const *items; /* list/table fields */
    size_t count;
    int source;               /* 0 = default, 1 = user */
} DnxEntry;

typedef int (*DnxEntryCallback)(const DnxEntry *entry, void *userdata);

/*
 * Reads default_file first and user_file second.  Missing files are ignored.
 * Therefore values in the user file naturally override values from the
 * default file when the callback assigns them to the same variable.
 *
 * DNX syntax:
 *   :section
 *       key: value
 *   *list
 *       "one"
 *       "two"
 *   %table
 *       "a", 1, 2.0
 *
 * INI [section] / key=value is also accepted for compatibility.
 */
int dnx_foreach_ff(const char *default_file, const char *user_file,
                   DnxEntryCallback callback, void *userdata);

/* Scalar convenience API. User file has precedence over default file. */
char *dnx_parseon(const char *filename, const char *path);
char *dnx_parseff(const char *default_file, const char *user_file,
                  const char *path);
const char *dnx_get_str_ff(const char *def_file, const char *usr_file,
                           const char *path, const char *fallback);
char *dnx_get_strdup_ff(const char *def_file, const char *usr_file,
                        const char *path, const char *fallback);
int dnx_get_int_ff(const char *def_file, const char *usr_file,
                   const char *path, int fallback);
unsigned int dnx_get_uint_ff(const char *def_file, const char *usr_file,
                             const char *path, unsigned int fallback);
float dnx_get_float_ff(const char *def_file, const char *usr_file,
                       const char *path, float fallback);
double dnx_get_double_ff(const char *def_file, const char *usr_file,
                         const char *path, double fallback);
bool dnx_get_bool_ff(const char *def_file, const char *usr_file,
                     const char *path, bool fallback);

#ifdef __cplusplus
}
#endif
#endif
