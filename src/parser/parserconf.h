#ifndef SOLUX_PARSERCONF_H
#define SOLUX_PARSERCONF_H

#include <stddef.h>

typedef enum {
    DNX_SCALAR = 0,
    DNX_LIST,
    DNX_TABLE
} DnxType;

typedef struct DnxEntry {
    DnxType type;
    const char *key;
    const char *value;
    const char **items;
    size_t count;
    int source; /* 0 = default, 1 = user */
} DnxEntry;

typedef int (*DnxEntryCallback)(const DnxEntry *entry, void *userdata);

int dnx_foreach_ff(const char *default_path, const char *user_path,
                   DnxEntryCallback callback, void *userdata);

/* Validate Python configuration files without applying any configuration. */
int dnx_validate_ff(const char *default_path, const char *user_path);

#endif
