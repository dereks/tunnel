/**
 * Copyright (c) 2014 Derek Simkowiak
 */

#ifndef TUNNEL_SERVER_H
#define TUNNEL_SERVER_H

#include "tunnel.h"

typedef struct TunnelServer {

    int listen_fd;

    // The event_base for accept() and shutdown events:
    struct event_base *libevent_base;

    // The event that notifies us of new clients connecting:
    struct event *on_accept_event;

    // A software-triggered event from a system signal, for shutdown:
    struct event *on_shutdown_event;

    // The last thread that was scheduled to accept a new connection:
    List *last_thread_link;

    // The list of sockets which need to be accept()ed by a worker thread:
    List *pending_socket_list;

    // Used to safely hand off socket_fds to worker threads:
    pthread_mutex_t *pending_socket_mutex;

    // This condition is used to to notify worker threads of new socket_fds:
    pthread_cond_t *pending_socket_cond;

    // The list of worker threads for this server:
    List *thread_list;

    // The CyaSSL context shared by all threads:
    CYASSL_CTX *cyassl_ctx;

    char *ini_filename;

    // A struct with the parsed .ini file:
    struct TunnelConfig *config;

    unsigned int ref_count;

} TunnelServer;


TunnelServer *tunnel_server_new(const char *ini_filename);
void tunnel_server_serve_forever(TunnelServer *server);
void tunnel_server_shutdown(TunnelServer *server);

void tunnel_server_ref(TunnelServer *server);
void tunnel_server_unref(TunnelServer *server);

#endif  // TUNNEL_SERVER_H
