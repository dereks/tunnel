/**
 * Copyright (c) 2014 Derek Simkowiak
 */
#include "tunnel_config.h"
#include "ini.h"
#include <stdbool.h>

// Tiny utility function:
static bool is_match(const char *section, const char *name,
                     const char *target_section, const char *target_name)
{
    return (strcmp(section, target_section) == 0) &&
           (strcmp(name, target_name) == 0);
}

static int ini_parse_handler(void* user, const char* section, const char* name,
                   const char* value)
{
    TunnelConfig *config = (TunnelConfig *)user;

    if (is_match(section, name, "main", "ssl_server_name")) {
        config->ssl_server_name = strdup(value);
    } else if (is_match(section, name, "main", "ssl_server_port")) {
        config->ssl_server_port = (uint16_t)atoi(value);
    } else if (is_match(section, name, "main", "destination_name")) {
        config->destination_name = strdup(value);
    } else if (is_match(section, name, "main", "destination_port")) {
        config->destination_port = strdup(value);
    } else if (is_match(section, name, "main", "thread_count")) {

        // We need at least one thread to run:
        config->thread_count = atoi(value);
        config->thread_count = MAX(config->thread_count, 1);

    } else if (is_match(section, name, "main", "buffer_size")) {
        config->buffer_size = (size_t)atol(value);
        // We need at least 1 byte of buffer space:
        config->buffer_size = MAX(config->buffer_size, 1);
    } else if (is_match(section, name, "ssl", "verify_locations")) {
        config->verify_locations = strdup(value);
    } else if (is_match(section, name, "ssl", "certificate_file")) {
        config->certificate_file = strdup(value);
    } else if (is_match(section, name, "ssl", "PrivateKey_file")) {
        config->PrivateKey_file = strdup(value);
    } else {
        return 0;  /* unknown section/name, error */
    }
    return 1;
}


TunnelConfig *tunnel_config_new(const char *filename)
{
    TunnelConfig *config;
    int result;
    
    config = calloc(1, sizeof(*config));
    if (config == NULL) { return NULL; }
    
    config->filename = strdup(filename);
    if (config->filename == NULL) { 
        free(config);
        return NULL;
    }
    
    result = ini_parse(config->filename, ini_parse_handler, config);
    if (result < 0) {
        log(LOG_ERR, "ini_parse(): Can't parse %s. Result: %d.",
            config->filename, result);
        // Free any strings that have already been strdup()ed:
        tunnel_config_free(config);
        return NULL;
    }

    return config;
}

void tunnel_config_free(TunnelConfig *config) {
    if (config == NULL) { return; }
    
    if (config->filename != NULL) { free(config->filename); }
    if (config->ssl_server_name != NULL) { free(config->ssl_server_name); }
    if (config->destination_name != NULL) { free(config->destination_name); }
    if (config->destination_port != NULL) { free(config->destination_port); }
    if (config->certificate_file != NULL) { free(config->certificate_file); }
    if (config->PrivateKey_file != NULL) { free(config->PrivateKey_file); }
    free(config);
}


