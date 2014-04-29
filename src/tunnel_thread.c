/**
 * Copyright (c) 2014 Derek Simkowiak
 */

#include "tunnel_thread.h"

// Called when thread->ref_count reaches zero:
static void tunnel_thread_free(TunnelThread *thread);
// Where the event loop runs:
static void *tunnel_thread_task(void *ptr);

// libevent callbacks:
static void on_shutdown(int socket_fd, short event, void *arg);
static void on_accept_dispatch(int socket_fd, short event, void *arg);

TunnelThread *tunnel_thread_new(TunnelServer *server)
{
    TunnelThread *thread;

    // Make sure a non-NULL *thread was passed in:
    if (server == NULL) { return NULL; }

    // Allocate the new TunnelThread instance:
    thread = calloc(1, sizeof(*thread));
    if (thread == NULL) { return NULL; }

    // Allocate the new libevent event_base for this thread:
    thread->libevent_base = event_base_new();
    if (thread->libevent_base == NULL) {
        free(thread);
        return NULL;
    }

    // Allocate the new pthread_t for this TunnelThread:
    thread->pthread = calloc(1, sizeof( *(thread->pthread) ));
    if (thread->pthread == NULL) {
        event_base_free(thread->libevent_base);
        free(thread);
        return NULL;
    }

    // The software-only 'event' the main thread uses to notify us:
    thread->on_accept_dispatch_event =
     event_new(thread->libevent_base, -1 /* dummy fd */,
              EV_PERSIST, on_accept_dispatch, thread);

    if (thread->on_accept_dispatch_event == NULL) {
        tunnel_server_unref(server);
        event_base_free(thread->libevent_base);
        free(thread->pthread);
        free(thread);

        return NULL;
    }

    // The software-only 'event' used to listen for shutdown:
    thread->on_shutdown_event =
     event_new(thread->libevent_base, -1 /* dummy fd */ ,
               EV_PERSIST, on_shutdown, thread);

    if (thread->on_shutdown_event == NULL) {
        event_free(thread->on_accept_dispatch_event);
        tunnel_server_unref(server);
        event_base_free(thread->libevent_base);
        free(thread->pthread);
        free(thread);

        return NULL;
    }

    // Grab and reference the passed-in server:
    thread->server = server;
    tunnel_server_ref(thread->server);

    // Set the client list to be an empty list:
    thread->client_list = NULL;

    // Set the initial reference count to one:
    thread->ref_count = 1;

    return thread;
}


int tunnel_thread_launch(TunnelThread *thread)
{
    if (thread == NULL) { return -1; }

    return pthread_create(thread->pthread, NULL, tunnel_thread_task, (void *)thread );
}


void tunnel_thread_ref(TunnelThread *thread)
{
    if (thread == NULL) { return; }
    thread->ref_count++;
}

void tunnel_thread_unref(TunnelThread *thread)
{
    if (thread == NULL) { return; }

    thread->ref_count--;

    if (thread->ref_count == 0) {
        tunnel_thread_free(thread);
    }
}


static void tunnel_thread_free(TunnelThread *thread)
{
    if (thread == NULL) { return; }

    // Drop our reference to the TunnelServer:
    tunnel_server_unref(thread->server);

    // Free the event_base for this thread:
    event_base_free(thread->libevent_base);

    // Dereference the server instance:
    tunnel_server_unref(thread->server);

    // By the time we get here, the client_list should be empty.
    // If it's not, log the error.
    if (thread->client_list != NULL) {
        log(LOG_WARNING, "TunnelThread 0x%p: client_list not NULL on free().", thread);
    }

    // Free the pthread:
    free(thread->pthread);
    thread->pthread = NULL;

    free(thread);
}


// This function is invoked in a new pthread by tunnel_thread_launch():
static void *tunnel_thread_task(void *ptr) {
    //
    // This event handler tries to make a paired socket connection
    // (in plaintext) to the destination.
    //
    // Then it sets up the libevent callbacks for read, write, and error
    // on both sockets.  Then the thread reacts to libevent notifications.
    //
    TunnelThread *thread = (TunnelThread *)ptr;

    struct timeval one_day = {72000, 0};  // Necessary for software events

    int result = -1;

    // Add the software-only events to our event_base:
    // Bug: software-only events require a timeout, or else they get ignored
    // by libevent (even though event_add() returns zero).
    result = event_add(thread->on_accept_dispatch_event, &one_day);
    result = event_add(thread->on_shutdown_event, &one_day);

    // Start the event loop.  This will block until killed with a signal.
    log(LOG_INFO, "Event loop started for TunnelThread 0x%p", thread);

    result = event_base_dispatch(thread->libevent_base);

    // We're back!
    log(LOG_INFO, "Event loop stopped for TunnelThread 0x%p.  Result: %d",
        thread, result);

    tunnel_thread_unref(thread);  // Done with this thread.
    pthread_exit(NULL);
}


static void on_accept_dispatch(int socket_fd, short event, void *arg) {
    // Bug: We ignore timeouts (forced on us by event_add()):
    if (event & EV_TIMEOUT) { return; }

    TunnelThread *thread = (TunnelThread *)arg;
    
    // The main thread told us there was a new pending socket.  Grab it.
    // Start critical section.  We manipulate the shared queue (linked list).
    List *link = NULL;
    int new_socket_fd = -1;

    log(LOG_INFO, "TunnelThread 0x%p received dispatch event.", thread);
    pthread_mutex_lock(thread->server->pending_socket_mutex);

    if (thread->server->pending_socket_list != NULL) {
        // Grab the first link off the list:
        link = thread->server->pending_socket_list;

        // Grab the socket_fd from that link:
        new_socket_fd = (long int)list_user_data(link);

        // Now delete this link:
        thread->server->pending_socket_list =
         list_delete_link(thread->server->pending_socket_list, link);
    }

    // End critical section.
    pthread_mutex_unlock(thread->server->pending_socket_mutex);

    // If we didn't get a new socket_fd, just return:
    if (new_socket_fd == -1) {
        log(LOG_WARNING, "TunnelThread 0x%p did not find new_socket_fd.", thread);
        return;
    }

    // We got a new socket, so connect a client to it:
    TunnelClient *client = tunnel_client_new(thread, thread->server);
    if (client == NULL) {
        log(LOG_WARNING, "Can't allocate a new TunnelClient instance.");
        return;  // Can't work without a *client.
    }

    thread->client_list = list_prepend(thread->client_list, client);
    // thread->client_list now points to the new list node.

    int result = tunnel_client_connect(client, new_socket_fd, thread->client_list);
    if (result != 0) {
        log(LOG_WARNING, "Can't connect client.");
        tunnel_client_free(client);
        return;
    }

}


static void on_shutdown(int socket_fd, short event, void *arg) {
    // Bug: We ignore timeouts (forced on us by event_add()):
    if (event & EV_TIMEOUT) { return; }

    // Tell all the clients to close connection and free themselves:
    TunnelClient *client;
    TunnelThread *thread = (TunnelThread *)arg;
    List *client_list = thread->client_list;

    while (client_list != NULL) {
        client = list_user_data(client_list);
        // Close all sockets and free resources:
        tunnel_client_disconnect_and_free(client);
        client_list = list_next(client_list);
    }

    // Remove the thread's on_shutdown event.  When all events
    // are event_del()'d, the event_base_dispatch() loop will exit.
    event_del(thread->on_shutdown_event);

#if 0
    // Redundant call to kill the worker loop.  If the shutdown event
    // was correctly event_del()'d then this is unneccessary.
    event_base_loopexit(thread->libevent_base, NULL);
#endif
}


