/**
 * Copyright (c) 2014 Derek Simkowiak
 */

#include "tunnel_server.h"

static void on_accept(int socket_fd, short event, void *arg);
static void on_shutdown(int socket_fd, short event, void *arg);
static void tunnel_server_free(TunnelServer *server);

TunnelServer *tunnel_server_new(const char *ini_filename)
{
    TunnelServer *server;
    int result;

    // calloc() means malloc() and memset() to 0x0:
    server = calloc(1, sizeof(*server));
    if (server == NULL) { return NULL; }

    server->ini_filename = strdup(ini_filename);
    if (server->ini_filename == NULL) {
        tunnel_server_free(server);
        return NULL;
    }

    server->config = tunnel_config_new(server->ini_filename);
    if (server->config == NULL) {
        tunnel_server_free(server);
        return NULL;
    }

    // Create the CYASSL_CTX:
    server->cyassl_ctx = CyaSSL_CTX_new(CyaSSLv23_server_method());
    if ( server->cyassl_ctx == NULL) {
        tunnel_server_free(server);
        return NULL;
    }

    // Load CA certificates into CYASSL_CTX:
    result =
     CyaSSL_CTX_load_verify_locations(server->cyassl_ctx,
                                      server->config->verify_locations, 0);
    if (result != SSL_SUCCESS) {
        log(LOG_ERR, "Error loading %s.", server->config->verify_locations);
        tunnel_server_free(server);
        return NULL;
    }

    result =
     CyaSSL_CTX_use_certificate_file(server->cyassl_ctx,
                                    server->config->certificate_file,
                                    SSL_FILETYPE_PEM);
    if (result != SSL_SUCCESS) {
        log(LOG_ERR, "Error loading %s.", server->config->certificate_file);
        tunnel_server_free(server);
        return NULL;
    }

    result =
     CyaSSL_CTX_use_PrivateKey_file(server->cyassl_ctx,
                                   server->config->PrivateKey_file,
                                   SSL_FILETYPE_PEM);
    if (result != SSL_SUCCESS) {
        log(LOG_ERR, "Error loading %s.", server->config->PrivateKey_file);
        tunnel_server_free(server);
        return NULL;
    }

    // Create the pthreads mutex and conditional for the worker's job queue:
    server->pending_socket_mutex = calloc(1, sizeof(*(server->pending_socket_mutex)));
    if (server->pending_socket_mutex == NULL) {
        tunnel_server_free(server);
        return NULL;
    }

    server->pending_socket_cond = calloc(1, sizeof(*(server->pending_socket_cond)));
    if (server->pending_socket_cond == NULL) {
        tunnel_server_free(server);
        return NULL;
    }

    // Set up Libevent for use with locking and thread ID functions:
    result = evthread_use_threads();
    if (result != 0) {
        tunnel_server_free(server);
        return NULL;
    }

    // Allocate the new libevent event_base for this thread:
    server->libevent_base = event_base_new();
    if (server->libevent_base == NULL) {
        tunnel_server_free(server);
        return NULL;
    }

    // The software-only 'event' used to listen for shutdown:
    server->on_shutdown_event =
     event_new(server->libevent_base, -1 /* dummy fd */,
              EV_PERSIST, on_shutdown, server);

    if (server->on_shutdown_event == NULL) {
        tunnel_server_free(server);
        return NULL;
    }

    server->last_thread_link = NULL;

    return server;
}

void tunnel_server_ref(TunnelServer *server)
{
    if (server == NULL) { return; }
    server->ref_count++;
}

void tunnel_server_unref(TunnelServer *server)
{
    if (server == NULL) { return; }

    server->ref_count--;

    if (server->ref_count == 0) {
        tunnel_server_free(server);
    }
}

static void tunnel_server_free(TunnelServer *server)
{
    if (server == NULL) { return; }
    
    // Note: server->on_accept_event is normally free()'d in serve_forever().
    if (server->on_accept_event != NULL) { event_free(server->on_accept_event); }

    // Free the server and its resources:
    if (server->on_shutdown_event != NULL) { event_free(server->on_shutdown_event); }
    if (server->libevent_base != NULL) { event_base_free(server->libevent_base); }
    if (server->pending_socket_cond != NULL) { free(server->pending_socket_cond); }
    if (server->pending_socket_mutex != NULL) { free(server->pending_socket_mutex); }
    if (server->cyassl_ctx != NULL) { CyaSSL_CTX_free(server->cyassl_ctx); }
    if (server->config != NULL) { tunnel_config_free(server->config); }
    if (server->ini_filename) { free(server->ini_filename); }
    free(server);
}

void tunnel_server_serve_forever(TunnelServer *server)
{
    //
    // Get the listening socket:
    //
    int listen_fd;

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        log_err("socket() failed (result: %d).", listen_fd);
        return;
    }

    // libevent sockets must be non-blocking:
    evutil_make_socket_nonblocking(listen_fd);

    //
    // Bind to the server address:
    //
    struct sockaddr_in bind_address;
    int result;
    
    memset(&bind_address, 0, sizeof(bind_address));

    bind_address.sin_family = AF_INET;

    // Get the bind IP address (or INADDR_ANY if it's "*"):
    if (strncmp("*", server->config->ssl_server_name, 1) == 0) {
        bind_address.sin_addr.s_addr = INADDR_ANY;
    } else {
        // Try to parse the user-supplied ssl_server_addr:
        result = inet_pton(AF_INET, server->config->ssl_server_name,
              &bind_address.sin_addr.s_addr);

        if (result != 1) {
            // Unable to parse the address.
            log(LOG_WARNING, "inet_pton() failed to parse \"%s\" (result: %d).",
                server->config->ssl_server_name, result);
            close(listen_fd);
            return;
        }
    }
    bind_address.sin_port = htons(server->config->ssl_server_port);

    result = bind(listen_fd, (struct sockaddr *)&bind_address, sizeof(bind_address));
    if (result < 0) {
        log_err("bind() failed.");
        close(listen_fd);
        return;
    }

    // Set the SO_REUSEADDR flag to true; this prevents the
    // "address is already is use" error when restarting the server quickly.
    int reuseaddr_flag = 0x1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_flag,
               sizeof(reuseaddr_flag));

    //
    // Launch the worker threads:
    //
    TunnelThread *thread = NULL;
    int index;

    for(index = 0 ; index < server->config->thread_count; index++) {

        thread = tunnel_thread_new(server);
        if (thread != NULL) {
            // Try to launch the new thread:
            result = tunnel_thread_launch(thread);
        }

        if (thread == NULL || result != 0) {
            // Allocation or launching of the thread failed.
            log(LOG_ERR, "tunnel_thread_[new()/launch()] failed.");

            // Free all previously-allocated threads:
            while(server->thread_list != NULL) {

                thread = list_user_data(server->thread_list);
                tunnel_thread_unref(thread);

                server->thread_list =
                 list_delete_link(server->thread_list, server->thread_list);
            }

            break;  // Break out with no threads in the server->thread_list.
        }

        // Success; store and launch the thread instance:
        server->thread_list = list_prepend(server->thread_list, thread);
        
        // FIXME: The reentrant call to event_base_dispatch causes a bug on launch:
        // [warn] event_base_loop: reentrant invocation.  Only one event_base_loop can run on each event_base at once.
        // Need to protect against that in the threads with a mutex.
        
        sleep(1);  // FIXME: Cheap hack, need a proper mutex instead
    }

    // Make sure we launched some threads:
    if (server->thread_list == NULL) {
        log(LOG_WARNING, "Launching threads failed.");
        close(listen_fd);
        return;
    }
    
    // Set the last_thread_link to a non-NULL value so we can iterate over it:
    server->last_thread_link = server->thread_list;

    // Allocate an EV_READ event to be notified when a client connects.
    server->on_accept_event =
     event_new(server->libevent_base, listen_fd, EV_READ | EV_PERSIST, on_accept,
              (void *)server);

    if (server->on_accept_event == NULL) {
        log(LOG_WARNING, "event_new() failed.");
        close(listen_fd);
        return;
    }

    // Add the on_shutdown_event to our event_base:
    event_add(server->on_accept_event, NULL);

    // Add the on_shutdown_event to our event_base:
    // Bug: software-only events require a timeout, or else they get ignored
    // by libevent (even though event_add() returns zero).
    event_add(server->on_shutdown_event, NULL);

    // Finally, listen to the socket and wait for events:
    result = listen(listen_fd, SOMAXCONN);
    if (result < 0) {
        log_err("listen() failed.");
        event_free(server->on_accept_event);
        close(listen_fd);
        return;
    }
    
    log(LOG_NOTICE, "TunnelServer running.");
    
    // Start the event loop for new connections.  This will block
    // until killed with a signal, or all events event_del()'d.
    event_base_dispatch(server->libevent_base);

    // We're back; clean up:
    log(LOG_NOTICE, "TunnelServer stopped.");

    event_free(server->on_accept_event);
    server->on_accept_event = NULL;

    // Close the listener socket:
    close(listen_fd);
//    close(server->listen_fd);
//    server->listen_fd = -1;
}



static void on_accept(int socket_fd, short event, void *arg) {
    TunnelServer *server = (TunnelServer *)arg;

    //
    // accept() the connection:
    //
    int client_socket_fd;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    client_socket_fd = accept(socket_fd, (struct sockaddr *)&client_addr, &client_len);
    if (client_socket_fd < 0) {
        log_err("accept() failed.");
        return;
    }

    // libevent sockets must be non-blocking:
    evutil_make_socket_nonblocking(client_socket_fd);

    //
    // Dispatch this new socket_fd to one of the worker threads.
    //

    // Start critical section.  We manipulate the shared queue (linked list).
    pthread_mutex_lock(server->pending_socket_mutex);

    server->pending_socket_list =
     list_prepend(server->pending_socket_list, (void *)((long)client_socket_fd));

    // End critical section.
    pthread_mutex_unlock(server->pending_socket_mutex);


    // We use a purely user-triggered event to notify the worker threads
    // in a threadsafe way. This depends on evthread_use_pthreads() or similar.
    //
    // We are forced to choose which event_base -- and thus, which worker
    // thread -- we want to use.  So we do a simple round-robin scheduler.
    List *thread_link = list_next(server->last_thread_link);
    if (thread_link == NULL) {
        // Reached the end of the thread list; start over at the head:
        thread_link = server->thread_list;
    }

    TunnelThread *thread = list_user_data(thread_link);
    
    log(LOG_INFO, "Notifying thread 0x%p of accepted socket %d.", thread, client_socket_fd);
    event_active(thread->on_accept_dispatch_event, EV_WRITE, 0);
}

void tunnel_server_shutdown(TunnelServer *server)
{
    // Interrupt the main accept() loop to invoke on_shutdown:
    event_active(server->on_shutdown_event, EV_WRITE, 0);
}

static void on_shutdown(int socket_fd, short event, void *arg) {
    // Bug: We ignore timeouts (forced on us by event_add()):
    if (event & EV_TIMEOUT) { return; }

    TunnelServer *server = (TunnelServer *)arg;

    TunnelThread *thread;
    List *list = server->thread_list;

    while (list != NULL) {
        thread = list_user_data(list);
        event_active(thread->on_shutdown_event, EV_WRITE, 0);

        list = list->next;
    }

    // Remove the server's on_accept and on_shutdown events.  When all
    // events are event_del()'d, the event_base_dispatch() loop will exit.
    event_del(server->on_accept_event);
    event_del(server->on_shutdown_event);

#if 0
    // Redundant call to kill the server accept loop.  If the events were
    // correctly event_del()'d then this is unneccessary.
    event_base_loopexit(server->libevent_base, NULL);
#endif
}

