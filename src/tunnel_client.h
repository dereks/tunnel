/**
 * Copyright (c) 2014 Derek Simkowiak
 */
#ifndef TUNNEL_CLIENT_H
#define TUNNEL_CLIENT_H

#include "tunnel.h"

typedef struct TunnelClient {
    
    int dest_socket_fd;     // Tunnel socket to destination (in plaintext)
    int ssl_socket_fd;      // Client socket (in SSL)
    struct sockaddr_in sockaddr_ssl;
    
    CYASSL *cyassl;         // SSL session info
    int ssl_accept_state;   // Set to SSL_SUCCESS when the handshake is complete
    
    struct TunnelServer *server;   // Has shared CA/cert and config data    
    struct TunnelThread *thread;   // Has this thread's eventbase for event registration

    // The libevent 'events' used to listen for socket readiness:
    struct event *on_read_ssl_event;
    struct event *on_read_dest_event;
    struct event *on_write_ssl_event;
    struct event *on_write_dest_event;

    // These software-only timeout events are used to throttle I/O if 
    // our buffer starts overflowing:
    struct event *read_ssl_timeout_event;
    struct event *read_dest_timeout_event;
    struct event *write_ssl_timeout_event;
    struct event *write_dest_timeout_event;
    
    // Buffers for reading/writing bytes between sockets:
    char *from_ssl_buffer;
    char *from_dest_buffer;
    
    // Circular buffer for reading/writing chunks.  The storage is the
    // the buffer arrays above.
    FIFO *from_ssl_fifo;
    FIFO *from_dest_fifo;
    
    // Pointer to our entry in thread->client_list.
    List *link;

} TunnelClient;


// Allocate:
TunnelClient *tunnel_client_new(struct TunnelThread *thread,
                                struct TunnelServer *server);

// Connect and register socket event callbacks:
int tunnel_client_connect(TunnelClient *client, int socket_fd, List *link);

// Convenience function:
void tunnel_client_disconnect_and_free(TunnelClient *client);

// Close all sockets, free all connection-specific resources.
void tunnel_client_disconnect(TunnelClient *client);

// Close just the destination connection:
void tunnel_client_disconnect_dest(TunnelClient *client);

// Close just the SSL connection:
void tunnel_client_disconnect_ssl(TunnelClient *client);

// Free all resources:
void tunnel_client_free(TunnelClient *client);


#endif    /* TUNNEL_CLIENT_H */
