/**
 * Copyright (c) 2014 Derek Simkowiak
 */
#ifndef TUNNEL_THREAD_H
#define TUNNEL_THREAD_H

#include "tunnel.h"

typedef struct TunnelThread {
    pthread_t *pthread;
    struct event_base *libevent_base;

    struct TunnelServer *server;   // Shared CA/cert / config for all threads
    List *client_list;             // The list of clients running in this thread

    // A software-triggered event from the main thread, for new sockets:
    struct event *on_accept_dispatch_event;

    // A software-triggered event from the main thread, for shutdown:
    struct event *on_shutdown_event;

    unsigned int ref_count;

} TunnelThread;


TunnelThread *tunnel_thread_new(struct TunnelServer *server);
int tunnel_thread_launch(TunnelThread *thread);

void tunnel_thread_ref(TunnelThread *thread);
void tunnel_thread_unref(TunnelThread *thread);

#endif  // TUNNEL_THREAD_H
