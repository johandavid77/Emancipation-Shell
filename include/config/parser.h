#ifndef EMANCIPATION_CONFIG_PARSER_H
#define EMANCIPATION_CONFIG_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include "config/config.h"

bool config_load_from_file(const char *path, struct config *out, char *errbuf, size_t errbufsz);
const char *config_default_path(void);

#endif /* EMANCIPATION_CONFIG_PARSER_H */
