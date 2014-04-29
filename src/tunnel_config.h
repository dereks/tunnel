/**
 * Copyright (c) 2014 Derek Simkowiak
 */
#ifndef TUNNEL_CONFIG_H
#define TUNNEL_CONFIG_H

#include "tunnel.h"

typedef struct TunnelConfig {

    // The name of the .ini file that was loaded:
    char *filename;
    
    // Host name / interface address to bind to:
    char *ssl_server_name;

    // FIXME: Use char * and gethostaddr() instead of inet_pton() w/short:
    uint16_t ssl_server_port;
    
    // The remote server to tunnel all traffic to:
    char *destination_name;
    char *destination_port;
    
    // The SSL Cert, Key, and CA Cert ("verify_locations") to use:
    char *verify_locations;  // For CyaSSL_CTX_load_verify_locations()
    char *certificate_file;  // For CyaSSL_CTX_use_certificate_file()
    char *PrivateKey_file;   // For CyaSSL_CTX_use_PrivateKey_file()
    
    // The number of worker threads to launch:
    int thread_count;
    
    // The size of the read/write buffers in RAM:
    size_t buffer_size;

} TunnelConfig;


TunnelConfig *tunnel_config_new(const char *filename);
void tunnel_config_free(TunnelConfig *config);


#endif	/* TUNNEL_CONFIG_H */
