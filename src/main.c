
#include <signal.h>
#include "tunnel.h"

// This server instance is accessible from the sighandler:
TunnelServer *server;

static void sighandler(int signal)
{
    tunnel_server_shutdown(server);
}

int main(int argc, char **argv)
{
    // Initialize CyaSSL:
    CyaSSL_Init();
    
    // Initialize syslog:
    setlogmask(LOG_UPTO(LOG_NOTICE));
    openlog("tunnel", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL1);

    // Initialize libevent in debug mode (optional):
    event_enable_debug_mode();
    evthread_enable_lock_debuging();

    // Register signal handlers:
    sigset_t signal_set;
    sigemptyset(&signal_set);
    struct sigaction action = {
        .sa_handler = sighandler,
        .sa_mask = signal_set,
        .sa_flags = SA_RESTART,
    };
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);


    // Instantiate a new tunnel server:
    server = tunnel_server_new("./tunnel.ini");

    // This blocks until a shutdown signal:
    tunnel_server_serve_forever(server);

    // We're back!  Do cleanup.  First, free the TunnerServer instance:
    tunnel_server_unref(server);

    // Close syslog:
    closelog();

    // Cleanup CyaSSL:
    CyaSSL_Cleanup();

    return 0;
}

