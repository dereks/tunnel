/**
 * Copyright (c) 2014 Derek Simkowiak
 */

#ifndef TUNNEL_H
#define TUNNEL_H

// External libraries:
#include <stdio.h>
#include <syscall.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/param.h>    // for MIN()/MAX() macros
#include <arpa/inet.h>    // for inet_pton()

#include <pthread.h>

#include <event2/event.h>
#include <event2/thread.h>
#include <event2/event-config.h>

#include <syslog.h>

#include <cyassl/ssl.h>
#include <cyassl/error-ssl.h>


#ifdef __linux__
  // Get the Linux LWP id, which is more useful than the opaque pthread ID:
  #define get_thread_id() syscall(SYS_gettid)
#else
  // Use the more generic pthread ID:
  #define get_thread_id() pthread_self()
#endif


// Cross-platform thread support:
#ifdef EVTHREAD_USE_WINDOWS_THREADS_IMPLEMENTED
  #define evthread_use_threads() evthread_use_windows_threads()
#elif EVTHREAD_USE_PTHREADS_IMPLEMENTED
  #define evthread_use_threads() evthread_use_pthreads()
#else
  #error "Neither Windows Threads nor pthreads are implemented."
#endif

#ifndef MAX
  #define MAX(a,b) (((a)>(b)) ? (a): (b))
#endif
#ifndef MIN
  #define MIN(a,b) (((a)<(b)) ? (a): (b))
#endif


// Used for standard application-level syslog errors:
#define log(level, fmt, args...)  do { \
    syslog(level, "[%ld]%s:%d:%s(): "fmt, get_thread_id(), __FILE__, __LINE__, __func__ , ##args); \
} while (0)

// Used for system errors reported with errno:
#define log_err(fmt, args...)  do { \
    char strerror_buffer[256]; \
    strerror_r(errno, strerror_buffer, sizeof(strerror_buffer)); \
    syslog(LOG_ERR, "[%ld]%s:%d:%s(): %s. " fmt, get_thread_id(), __FILE__, __LINE__, __func__, strerror_buffer , ##args); \
} while (0)

// Utilities:
#include "list.h"
#include "fifo.h"

// Tunnel API:
#include "tunnel_config.h"
#include "tunnel_client.h"
#include "tunnel_thread.h"
#include "tunnel_server.h"


#endif  /* TUNNEL_H */
