/**
 * Copyright (c) 2014 Derek Simkowiak
 */

#include "tunnel_client.h"

static void on_read_ssl(int socket_fd, short event, void *arg);
static void on_write_ssl(int socket_fd, short event, void *arg);

static void on_read_dest(int socket_fd, short event, void *arg);
static void on_write_dest(int socket_fd, short event, void *arg);

// These timeout callbacks re-enable the read/write events.
static void on_read_ssl_timeout(int socket_fd, short event, void *arg);
static void on_write_ssl_timeout(int socket_fd, short event, void *arg);
static void on_read_dest_timeout(int socket_fd, short event, void *arg);
static void on_write_dest_timeout(int socket_fd, short event, void *arg);

static void handle_ssl_accept(TunnelClient *client);


void tunnel_client_disconnect_and_free(TunnelClient *client) 
{
    tunnel_client_disconnect(client);
    tunnel_client_free(client);    
}

void tunnel_client_disconnect(TunnelClient *client) 
{
    tunnel_client_disconnect_dest(client);
    tunnel_client_disconnect_ssl(client);
}

void tunnel_client_disconnect_dest(TunnelClient *client) 
{
    if (client == NULL) { return; }
    
    // Close the socket:
    if (client->dest_socket_fd > 0) {
        close(client->dest_socket_fd);
        client->dest_socket_fd = -1;
    }
    
    // Unschedule the events event_add()ed for this connection:
    if (client->on_read_dest_event != NULL) { 
        event_del(client->on_read_dest_event); 
        event_free(client->on_read_dest_event);
        client->on_read_dest_event = NULL;
    }
    if (client->on_write_dest_event != NULL) { 
        event_del(client->on_write_dest_event); 
        event_free(client->on_write_dest_event);
        client->on_write_dest_event = NULL;
    }

    // Unlink us from the parent thread's client_list:
    if (client->link != NULL) {
        client->thread->client_list =
         list_delete_link(client->thread->client_list, client->link);
        client->link = NULL;
    }
}

void tunnel_client_disconnect_ssl(TunnelClient *client) 
{

    if (client == NULL) { return; }
    
    // Close the socket:
    if (client->ssl_socket_fd > 0) {
        close(client->ssl_socket_fd);
        client->ssl_socket_fd = -1;
    }
    
    // Unschedule the events event_add()ed for this connection:
    if (client->on_read_ssl_event != NULL) { 
        event_del(client->on_read_ssl_event); 
        event_free(client->on_read_ssl_event);
        client->on_read_ssl_event = NULL;
    }
    if (client->on_write_ssl_event != NULL) { 
        event_del(client->on_write_ssl_event); 
        event_free(client->on_write_ssl_event);        
        client->on_write_ssl_event = NULL;
    }

    // Unlink us from the parent thread's client_list:
    if (client->link != NULL) {
        client->thread->client_list =
         list_delete_link(client->thread->client_list, client->link);
        client->link = NULL;
    }
}

void tunnel_client_free(TunnelClient *client) 
{    
    if (client == NULL) { return; }

    if (client->read_ssl_timeout_event != NULL) {
        event_free(client->read_ssl_timeout_event);
    }
    if (client->read_dest_timeout_event != NULL) {
        event_free(client->read_dest_timeout_event);
    }
    if (client->write_ssl_timeout_event != NULL) {
        event_free(client->write_ssl_timeout_event);
    }
    if (client->write_dest_timeout_event != NULL) {
        event_free(client->write_dest_timeout_event);
    }
    
    if (client->cyassl != NULL) { CyaSSL_free(client->cyassl); }
    if (client->from_ssl_buffer != NULL) { free(client->from_ssl_buffer); }
    if (client->from_dest_buffer != NULL) { free(client->from_dest_buffer); }

    tunnel_thread_unref(client->thread);
    tunnel_server_unref(client->server);
    
    fifo_free(client->from_ssl_fifo);
    fifo_free(client->from_dest_fifo);

    free(client);
}

TunnelClient *tunnel_client_new(TunnelThread *thread, TunnelServer *server)
{

    TunnelClient *client;
    
    if (thread == NULL || server == NULL) { return NULL; }

    client = calloc(1, sizeof(*client));
    if (client == NULL) { return NULL; }
    
    // New CYALSSL * for this connection:
    client->cyassl = CyaSSL_new(server->cyassl_ctx);
    if (client->cyassl == NULL) {
        tunnel_client_free(client);
        return NULL;
    }
    
    // Allocate the buffers and FIFO for read/writes:
    client->from_ssl_buffer = calloc(1, server->config->buffer_size);
    if (client->from_ssl_buffer == NULL) {
        tunnel_client_free(client);
        return NULL;
    }
    
    client->from_dest_buffer = calloc(1, server->config->buffer_size);
    if (client->from_dest_buffer == NULL) {
        tunnel_client_free(client);
        return NULL;
    }
    
    client->from_ssl_fifo = fifo_new(server->config->buffer_size);
    if (client->from_ssl_fifo == NULL) {
        tunnel_client_free(client);
        return NULL;
    }
    
    client->from_dest_fifo = fifo_new(server->config->buffer_size);
    if (client->from_dest_fifo == NULL) {
        tunnel_client_free(client);
        return NULL;
    }    

    // Set the thread and server:
    client->thread = thread;
    tunnel_thread_ref(thread);
    
    client->server = server;    
    tunnel_server_ref(server);

    // These software-only events are only event_add()'d if our buffers fill up:
    client->read_ssl_timeout_event =
     event_new(client->thread->libevent_base, -1, 0x0, on_read_ssl_timeout,
               client);
    if (client->read_ssl_timeout_event == NULL) {
        tunnel_client_free(client);
        return NULL;
    }

    client->read_dest_timeout_event = 
     event_new(client->thread->libevent_base, -1, 0x0, on_read_dest_timeout,
               client);
    if (client->read_dest_timeout_event == NULL) {
        tunnel_client_free(client);
        return NULL;
    }

    client->write_ssl_timeout_event = 
     event_new(client->thread->libevent_base, -1, 0x0, on_write_ssl_timeout,
               client);
    if (client->write_ssl_timeout_event == NULL) {
        tunnel_client_free(client);
        return NULL;
    }

    client->write_dest_timeout_event = 
     event_new(client->thread->libevent_base, -1, 0x0, on_write_dest_timeout,
               client);
    if (client->write_dest_timeout_event == NULL) {
        tunnel_client_free(client);
        return NULL;
    }
    
    // These get set on connect:
    client->ssl_socket_fd = -1;
    client->dest_socket_fd = -1;

    // This flag lets us know when the handshake has completed:
    client->ssl_accept_state = SSL_ERROR_WANT_READ;    

    // These get set on connect.  (We need the accept()ed socket_fd first.)
    client->on_read_ssl_event = NULL;
    client->on_read_dest_event = NULL;
    client->on_write_ssl_event = NULL;
    client->on_write_dest_event = NULL;
    
    return client;
}


// socket_fd must be the client socket back returned by accept().
int tunnel_client_connect(TunnelClient *client, int socket_fd, List *link)
{
    int result;
    
    // First, we connect to the destination server.  We want to make sure
    // we can connect before we accept() an SSL connection.
    struct addrinfo hints;             // input (family / socktype)
    struct addrinfo *result_addrinfo;  // output (head of a linked list)
    struct addrinfo *next_addrinfo;    // linked list iterator

    memset(&hints, 0x0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;      // IPv6, IPv4, whatevah
    hints.ai_socktype = SOCK_STREAM;  // TCP only

    result =
     getaddrinfo(client->server->config->destination_name,
                client->server->config->destination_port, &hints,
                &result_addrinfo);

    if (result != 0) {
        log(LOG_ERR, "getaddrinfo: %s", gai_strerror(result));
        return -1;
    }

    // loop through all the results and connect to the first we can
    for (next_addrinfo = result_addrinfo; next_addrinfo != NULL;
         next_addrinfo = next_addrinfo->ai_next) {

        // Get a socket with this address:
        client->dest_socket_fd =
         socket(next_addrinfo->ai_family, next_addrinfo->ai_socktype,
               next_addrinfo->ai_protocol);
        
        if (client->dest_socket_fd == -1) {
            log_err("socket() failed.");  // Not a valid address and/or port.
            continue;
        }
        
        result =
         connect(client->dest_socket_fd, next_addrinfo->ai_addr,
                next_addrinfo->ai_addrlen);

        if (result == -1) {
            log_err("connect() attempt failed.");
            close(client->dest_socket_fd);  // Free the socket resources
            continue;
        }

        break; // Successful connect.  Note, next_addrinfo != NULL.
    }

    freeaddrinfo(result_addrinfo);  // This was malloc()'d by getaddrinfo().

    if (next_addrinfo == NULL) {
        log_err("connect() failed on all addrinfos for %s:%s.",
                client->server->config->destination_name,
                client->server->config->destination_port);
        return -1;
    }
    
    // Set up our libevent callbacks for this socket:
    client->on_read_dest_event =
     event_new(client->thread->libevent_base, client->dest_socket_fd,
              EV_READ | EV_PERSIST, on_read_dest, client);

    if (client->on_read_dest_event == NULL) { 
        log(LOG_WARNING, "event_new() failed.");
        tunnel_client_disconnect(client); // Free the socket resources
        return -3; 
    }
    
    // Write events are armed on-demand; they do not use EV_PERSIST.
    client->on_write_dest_event =
     event_new(client->thread->libevent_base, client->dest_socket_fd,
              EV_WRITE, on_write_dest, client);

    if (client->on_write_dest_event == NULL) {
        log(LOG_WARNING, "event_new() failed.");
        tunnel_client_disconnect(client); // Free the socket resources
        return -4;
    }

    client->ssl_socket_fd = socket_fd;
    
    // libevent sockets must be non-blocking:
    evutil_make_socket_nonblocking(client->ssl_socket_fd);
    evutil_make_socket_nonblocking(client->dest_socket_fd);
    
    // Associate the SSL socket with CyaSSL:
    CyaSSL_set_fd(client->cyassl, client->ssl_socket_fd);
    CyaSSL_set_using_nonblock(client->cyassl, 1);
    
    // Set up our libevent callbacks for this socket, using the thread-wide
    // libevent event_base:
    client->on_read_ssl_event =
     event_new(client->thread->libevent_base, client->ssl_socket_fd,
              EV_READ | EV_PERSIST, on_read_ssl, client);

    if (client->on_read_ssl_event == NULL) { 
        log(LOG_WARNING, "event_new() failed.");
        tunnel_client_disconnect(client); // Free the socket resources
        return -3; 
    }
    
    // Write events are armed on-demand; they do not use EV_PERSIST.
    client->on_write_ssl_event =
     event_new(client->thread->libevent_base, client->ssl_socket_fd,
              EV_WRITE, on_write_ssl, client);

    if (client->on_write_ssl_event == NULL) {
        log(LOG_WARNING, "event_new() failed.");
        tunnel_client_disconnect(client); // Free the socket resources
        return -4;
    }

    // Save our list node in thread->client_list.  This is just so we
    // can list_delete() ourselves without searching through the list first.
    client->link = link;
    
    // Write events are added on-demand when bytes are ready in the fifo.
    event_add(client->on_read_dest_event, NULL);
    event_add(client->on_read_ssl_event, NULL);

    return 0;
}

static void on_read_ssl(int socket_fd, short event, void *arg) {

    TunnelClient *client = (TunnelClient *)arg;

    log(LOG_DEBUG, "Entered.");
    
    if (client->ssl_accept_state != SSL_SUCCESS) {
        log(LOG_DEBUG, "SSL NOT accepted.");
        handle_ssl_accept(client);

        // Now are we done?
        if (client->ssl_accept_state != SSL_SUCCESS) {
            // Nope, come back when ready.
            return;
        }
    }

    // Before reading, make sure we have room in our buffer:
    if (fifo_bytes_free(client->from_ssl_fifo) == 0) {
        // The client is not draining bytes fast enough.  Take a breather.
        struct timeval one_ms = {0,1000};
        event_del(client->on_read_ssl_event);  // Halt these for a bit
        event_add(client->read_ssl_timeout_event, &one_ms);
        return;
    }
    
    int ssl_read_result, ssl_error;
    size_t write_index;
    char *buffer_addr;
    size_t buffer_size;

    do {
        write_index = fifo_write_index(client->from_ssl_fifo);
        buffer_size = fifo_write_size(client->from_ssl_fifo);
        buffer_addr = &(client->from_ssl_buffer[write_index]);
        
        ssl_read_result = CyaSSL_read(client->cyassl, buffer_addr, buffer_size);

        // We just read bytes from the socket (with CyaSSL_read()) and 
        // put them into the client->from_ssl_buffer.  Record those new
        // bytes in the FIFO:
        if (ssl_read_result > 0) {
            fifo_write(client->from_ssl_fifo, ssl_read_result);
        }
        
    } while ( (ssl_read_result > 0) && (fifo_bytes_free(client->from_ssl_fifo) > 0) );
    
    // ssl_read_result finally reached <= 0.
    ssl_error = CyaSSL_get_error(client->cyassl, 0);

    if (fifo_bytes_used(client->from_ssl_fifo) > 0) {
        // We have some pending bytes.  Wait for on_write_dest readiness:
        log(LOG_DEBUG, "fifo_bytes_used(client->from_ssl_fifo): %ld.  Scheduling on_write_dest_event.", fifo_bytes_used(client->from_ssl_fifo));
        event_add(client->on_write_dest_event, NULL);
    }
    
    if (ssl_error == SSL_ERROR_WANT_READ) {
        log(LOG_DEBUG, "SSL_ERROR_WANT_READ: Returning.");
        return;  // Success.
    }

    if (ssl_error == SSL_ERROR_WANT_WRITE) {
        log(LOG_DEBUG, "SSL_ERROR_WANT_WRITE: Scheduling on_write_ssl_event, returning.");
        event_add(client->on_write_ssl_event, NULL);
        return;  // Success.
    }

    // A real read error (or disconnect, or "close notify alert") occurred.
    log(LOG_NOTICE, "%s",
        CyaSSL_ERR_error_string(ssl_error, client->from_ssl_buffer));
        
    if (fifo_bytes_used(client->from_dest_fifo) > 0) {
        // We still have pending bytes to write; send them before
        // closing the SSL socket.
        log(LOG_DEBUG,
            "fifo_bytes_used(client->from_dest_fifo): %ld.  Scheduling on_write_ssl_event.",
            fifo_bytes_used(client->from_ssl_fifo));
        event_add(client->on_write_ssl_event, NULL);
    } else {
        log(LOG_WARNING,
            "fifo_bytes_used(client->from_dest_fifo) is zero. Closing SSL connection.");
        tunnel_client_disconnect_ssl(client);

        if (fifo_bytes_used(client->from_ssl_fifo) == 0) {
            // All bytes have been flushed.  Done.
            log(LOG_NOTICE, "Closing all connections.");
            tunnel_client_disconnect_and_free(client);
            return;
        }
    }
}


static void on_write_ssl(int socket_fd, short event, void *arg) {

    TunnelClient *client = (TunnelClient *)arg;

    log(LOG_DEBUG, "Entered. fifo_bytes_used(client->from_dest_fifo): %ld",
        fifo_bytes_used(client->from_dest_fifo));

    if (client->ssl_accept_state != SSL_SUCCESS) {
        log(LOG_DEBUG, "SSL NOT accepted.");
        handle_ssl_accept(client);
        
        // Now are we done?
        if (client->ssl_accept_state != SSL_SUCCESS) {
            // Nope, come back when ready.
            return;
        }
    }

    int ssl_write_result, ssl_error;
    size_t read_index;
    char *buffer_addr;
    size_t buffer_size;

    read_index = fifo_read_index(client->from_dest_fifo);
    buffer_size = fifo_read_size(client->from_dest_fifo);
    buffer_addr = &(client->from_dest_buffer[read_index]);

    ssl_write_result = CyaSSL_write(client->cyassl, buffer_addr, buffer_size);
    
    while ( (ssl_write_result > 0) && (fifo_bytes_used(client->from_dest_fifo) > 0) ) {
        // We just wrote bytes from the from_dest_fifo (with CyaSSL_write()).  
        // Count those processed bytes with the FIFO index counter:
        log(LOG_DEBUG, "wrote %d bytes", ssl_write_result);
        fifo_read(client->from_dest_fifo, ssl_write_result);
        
        read_index = fifo_read_index(client->from_dest_fifo);
        buffer_size = fifo_read_size(client->from_dest_fifo);
        buffer_addr = &(client->from_dest_buffer[read_index]);
        
        ssl_write_result = CyaSSL_write(client->cyassl, buffer_addr, buffer_size);
    }
    log(LOG_DEBUG, "Last ssl_write_result: %d", ssl_write_result);
    
    // ssl_write_result finally reached <= 0.
    ssl_error = CyaSSL_get_error(client->cyassl, 0);

    if (ssl_error == SSL_ERROR_WANT_READ) {
        log(LOG_DEBUG, "SSL_ERROR_WANT_READ");
        
        if (client->dest_socket_fd == -1) {
            log(LOG_INFO, "Destination has closed, so closing SSL connection.");
            tunnel_client_disconnect_and_free(client);
        }
        return;  // Success.
    }

    if (ssl_error == SSL_ERROR_WANT_WRITE) {
        log(LOG_DEBUG, "SSL_ERROR_WANT_WRITE");

        // If we are not draining bytes, we should take a breather first.
        if (fifo_bytes_used(client->from_dest_fifo) > 0) {
            // The client is not draining bytes fast enough.  Take a breather.
            struct timeval one_ms = {0, 1000};
            log(LOG_DEBUG, "Scheduling write_ssl_timeout_event, returning.");
            event_add(client->write_ssl_timeout_event, &one_ms);
            return;
        }
        log(LOG_DEBUG, "Scheduling on_write_ssl_event, returning.");
        event_add(client->on_write_ssl_event, NULL);
        return;  // Success.
    }

    // A real write error or disconnect occurred.
    log(LOG_NOTICE, "%s",
        CyaSSL_ERR_error_string(ssl_error, client->from_ssl_buffer));

    log(LOG_INFO, "Closing SSL connection.");
    tunnel_client_disconnect_ssl(client);
    
    if (fifo_bytes_used(client->from_ssl_fifo) == 0) {
        // All bytes have been flushed.  Done.
        log(LOG_NOTICE, "Closing all connections.");
        tunnel_client_disconnect_and_free(client);
        return;
    } else {
         // We have some pending bytes.  Let on_write do the cleanup:
        log(LOG_WARNING, "%ld pending bytes in from_ssl_fifo.  Adding on_write_dest_event.", fifo_bytes_used(client->from_ssl_fifo));
        event_add(client->on_write_dest_event, NULL);
        return;
   }
}


static void on_read_dest(int socket_fd, short event, void *arg) {

    log(LOG_DEBUG, "Entered.");

    TunnelClient *client = (TunnelClient *)arg;
    
    // First, make sure we have room in our buffer:
    if (fifo_bytes_free(client->from_dest_fifo) == 0) {
        // The client is not draining bytes fast enough.  Take a breather.
        struct timeval one_ms = {0, 1000};
        event_del(client->on_read_dest_event);  // Halt these for a bit
        event_add(client->read_dest_timeout_event, &one_ms);
        return;
    }

    int read_result;
    size_t write_index;
    char *buffer_addr;
    size_t buffer_size;
    
    do {
        write_index = fifo_write_index(client->from_dest_fifo);
        buffer_size = fifo_write_size(client->from_dest_fifo);
        buffer_addr = &(client->from_dest_buffer[write_index]);

        read_result = read(client->dest_socket_fd, buffer_addr, buffer_size);
        log(LOG_DEBUG, "read_result: %d", read_result);
        
        // We just read bytes from the dest socket (with read()) and 
        // put them into the client->from_dest_buffer.  Record those new
        // bytes in the FIFO:
        if (read_result > 0) {
            fifo_write(client->from_dest_fifo, read_result);
        }
        
    } while ( (read_result > 0) && fifo_bytes_free(client->from_dest_fifo) > 0);

    log(LOG_DEBUG,
        "Done reading. read_result: %d, fifo_bytes_free(client->from_dest_fifo): %ld",
        read_result, fifo_bytes_free(client->from_dest_fifo));
        
    // See if we need to write to the SSL socket:
    if (fifo_bytes_used(client->from_dest_fifo) > 0) {
        // We have some pending bytes.  Wait for on_write readiness:
        log(LOG_WARNING, "%ld pending bytes in from_dest_fifo.  Adding on_write_ssl_event.", fifo_bytes_used(client->from_dest_fifo));
        event_add(client->on_write_ssl_event, NULL);
    }
    
    // See if our buffer is full (so, read_result > 0).  If so, ignore errno
    // and let the next on_read_dest_event schedule the timeout:
    if (fifo_bytes_free(client->from_dest_fifo) == 0) {
        // The client is not draining bytes fast enough.  Take a breather.
        log(LOG_DEBUG, "Returning due to full buffer.  (Ignoring errno.)");
        return;
    }

    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
        // We can safely ignore EAGAIN or EWOULDBLOCK:
        log(LOG_WARNING, "EAGAIN || EWOULDBLOCK; returning.");
        return;
    } else {
        // A real read error or disconnect occurred.
        log_err("read() returned %d.", read_result);
        
        // Close the socket.  When the FIFO is flushed, disconnect_and_free:
        log(LOG_WARNING, "Closing dest connection.");
        tunnel_client_disconnect_dest(client);
        
        if (fifo_bytes_used(client->from_dest_fifo) == 0) {
            // All bytes have been flushed.  Done.
            log(LOG_WARNING, "Closing all connections.");
            tunnel_client_disconnect_and_free(client);
            return;
        }
    }

}

static void on_write_dest(int socket_fd, short event, void *arg) {

    log(LOG_DEBUG, "Entered.");

    TunnelClient *client = (TunnelClient *)arg;
    int write_result;
        
    size_t read_index;
    char *buffer_addr;
    size_t buffer_size;

    do {    
        read_index = fifo_read_index(client->from_ssl_fifo);
        buffer_size = fifo_read_size(client->from_ssl_fifo);
        buffer_addr = &(client->from_ssl_buffer[read_index]);

        write_result = write(client->dest_socket_fd, buffer_addr, buffer_size);
        log(LOG_DEBUG, "write_result: %d", write_result);
        
        // We just wrote bytes from the from_ssl_fifo (with write()).  
        // Record those processed bytes with the FIFO index counter:
        if (write_result > 0) {
            fifo_read(client->from_ssl_fifo, write_result);
        }
        
    } while ( (write_result > 0) && (fifo_bytes_used(client->from_ssl_fifo) > 0) );

    log(LOG_DEBUG,
        "Done writing. write_result: %d, fifo_bytes_used(client->from_ssl_fifo): %ld",
        write_result, fifo_bytes_used(client->from_ssl_fifo));
    
    //
    // write_result <= 0
    //
    // Note: write_result == 0 is not an error.  It just means no bytes written,
    // which happens (e.g.) if the other end can't keep up.
    //
    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
        // We can safely ignore EAGAIN or EWOULDBLOCK.
        log(LOG_DEBUG, "(errno == EAGAIN) || (errno == EWOULDBLOCK)");

        // If we are not draining bytes, we should take a breather first.
        if (fifo_bytes_used(client->from_ssl_fifo) > 0) {
            // The client is not draining bytes fast enough.  Take a breather.
            log(LOG_DEBUG, "Scheduling write_dest_timeout_event due to fifo_bytes_used(client->from_ssl_fifo): %ld", fifo_bytes_used(client->from_ssl_fifo));
            struct timeval one_ms = {0, 1000};
            event_add(client->write_dest_timeout_event, &one_ms);
            return;
        }
        // No need to schedule a write; the from_ssl_fifo is empty.
        log(LOG_DEBUG, "fifo_bytes_used(client->from_ssl_fifo) == 0.  Returning.");
        return;

    } else {
        // A real write error or disconnect occurred.
        log_err("write() result: %d.", write_result);

        if (fifo_bytes_used(client->from_ssl_fifo) > 0) {
            // We still have some pending bytes.  Wait for on_dest_write:
            log(LOG_DEBUG, "Pending bytes in from_ssl_fifo.  Adding on_write_dest_event.");
            event_add(client->on_write_dest_event, NULL);
        } else {
            // The FIFO is flushed.  Is the other end disconnected?
            log(LOG_NOTICE, "Closing dest connection.");
            tunnel_client_disconnect_dest(client);
            if (client->ssl_socket_fd == -1) {
                log(LOG_NOTICE, "Closing all connections.");
                tunnel_client_disconnect_and_free(client);
                return;
            }
        }
    }
}
 

static void on_write_ssl_timeout(int socket_fd, short event, void *arg)
{
    TunnelClient *client = (TunnelClient *)arg;

    if (client->ssl_socket_fd == -1) {
        // The connection was closed by an ssl_read() while we were waiting:
        return;
    }

    log(LOG_DEBUG, "fifo_bytes_used(client->from_dest_fifo): %ld",
        fifo_bytes_used(client->from_dest_fifo));
    
    if (fifo_bytes_used(client->from_dest_fifo) > 0) {
        log(LOG_DEBUG, "Scheduling client->on_write_dest_event.");
        event_add(client->on_write_ssl_event, NULL);
    }
    return;
}


static void on_write_dest_timeout(int socket_fd, short event, void *arg)
{
    TunnelClient *client = (TunnelClient *)arg;

    if (client->dest_socket_fd == -1) {
        // The connection was closed by a read() while we were waiting:
        return;
    }

    log(LOG_DEBUG, "fifo_bytes_used(client->from_ssl_fifo): %ld",
        fifo_bytes_used(client->from_ssl_fifo));
    
    if (fifo_bytes_used(client->from_ssl_fifo) > 0) {
        log(LOG_DEBUG, "Scheduling client->on_write_dest_event.");
        event_add(client->on_write_dest_event, NULL);
    }
    return;
}


static void on_read_dest_timeout(int socket_fd, short event, void *arg)
{
    TunnelClient *client = (TunnelClient *)arg;
    struct timeval one_ms = {0, 1000};

    if (client->dest_socket_fd == -1) {
        // The connection was closed by an write() while we were waiting:
        return;
    }

    log(LOG_DEBUG, "fifo_bytes_free(client->from_dest_fifo): %ld",
        fifo_bytes_free(client->from_dest_fifo));
    
    if (fifo_bytes_free(client->from_dest_fifo) > 0) {
        log(LOG_DEBUG, "Restoring client->on_read_dest_event.");
        event_add(client->on_read_dest_event, NULL);
    } else {
        // Still no room in the buffer; wait longer.
        log(LOG_DEBUG, "Still no room; Waiting longer.");
        event_add(client->read_dest_timeout_event, &one_ms);
    }
    return;
}


static void on_read_ssl_timeout(int socket_fd, short event, void *arg)
{
    TunnelClient *client = (TunnelClient *)arg;
    struct timeval one_ms = {0, 1000};
    
    if (client->ssl_socket_fd == -1) {
        // The connection was closed by an ssl_write() while we were waiting:
        return;
    }

    log(LOG_DEBUG, "fifo_bytes_free(client->from_ssl_fifo): %ld",
        fifo_bytes_free(client->from_ssl_fifo));
    
    if (fifo_bytes_free(client->from_ssl_fifo) > 0) {
        log(LOG_DEBUG, "Restoring client->on_read_dest_event.");
        event_add(client->on_read_ssl_event, NULL);
    } else {
        // Still no room in the buffer; wait longer.
        log(LOG_DEBUG, "Still no room; Waiting longer.");
        event_add(client->read_ssl_timeout_event, &one_ms);
    }
    return;
}


static void handle_ssl_accept(TunnelClient *client)
{
    // New connection: Resume non-blocking calls to CyaSSL_accept():
    int ssl_accept_result = CyaSSL_accept(client->cyassl);
    int ssl_error = CyaSSL_get_error(client->cyassl, 0);

    // See if this is a real error, or just a WANT for more data:
    if (ssl_accept_result != SSL_SUCCESS) {
    
        if (ssl_error == SSL_ERROR_WANT_READ) {
            log(LOG_DEBUG, "SSL_ERROR_WANT_READ (handshake not complete).");
            return;
        }

        if (ssl_error == SSL_ERROR_WANT_WRITE) {
            log(LOG_DEBUG,
                "SSL_ERROR_WANT_WRITE (handshake not complete). "
                "Scheduling on_write_ssl_event.");
            event_add(client->on_write_ssl_event, NULL);
            return;
        }

        // There was a real error during the SSL handshake.
        log(LOG_NOTICE, "%s",
            CyaSSL_ERR_error_string(ssl_error, client->from_ssl_buffer));

        log(LOG_INFO, "Closing SSL connection.");
        tunnel_client_disconnect_and_free(client);
        return;

    } else {
        // SSL_SUCCESS!  Continue by tunneling bytes.
        log(LOG_DEBUG, "SSL connected.");
        client->ssl_accept_state = SSL_SUCCESS;
        return;
    }        
}

