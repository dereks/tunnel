Tunnel
======
# A lightweight SSL terminator.

Tunnel is a small SSL server (35K stripped) which decrypts and sends 
all plaintext bytes to the configured destination. 

## Features

Tunnel has:

- CyaSSL for SSL support.
- libevent for fast event-driven (non-blocking) I/O.
- pthreads for separate event loops on each core.
- syslog for logging.
- configuration via a simple .ini file.
- Shared instance reference counts for memory management.


## Event-based Architecture

Tunnel uses a modern event-based server architecture known as an "event pump",
or "readiness change notification loop". This is implemented in a 
cross-platform manner using libevent.

In contrast with Tunnel, most classic server architectures use the fork() or 
worker-thread-per-client model. Every new socket connection is handed off to 
a thread that isdedicated to that particular client for the life of 
the connection.

This classic design is common because it allows for the use of blocking I/O, 
which is easier and less error-prone than non-blocking I/O, and because it 
minimizes the number of sockets that any given worker thread needs to poll 
for readiness (using select() or poll()). Sometimes polling does use 
non-blocking I/O, with one thread handling many socket connections 
(but still handing off buffered bytes to worker threads for processing).

Tunnel uses only a single thread (per CPU core -- more on that below). This 
event loop handles all non-blocking socket I/O, processing bytes as soon 
as they're ready without the need for performance-killing context switches 
or mutex contention.

This is possible thanks to newer features available in modern Unix kernels:
epoll() on Linux, kqueue on BSD, and /dev/poll on Solaris. These features allow
the kernel to notify the program when a socket is ready, instead of having the
program poll() the large list of connected clients repeatedly. This provides 
substantial performance gains over select() and poll(): several hundred 
times faster socket I/O, and several thousand times faster on busy servers.

The library libevent abstracts the underlying OS-specific kernel feature into a 
cross-platform API, so Tunnel should run on any platform supported by
CyaSSL, pthreads, and libevent (including a fall-back to select() under 
Windows).

The one drawback of most event pump servers (like thttpd, lighttpd, 
snap-server, etc.) is that there is only a single event pump loop, meaning, 
on a multi-core CPU, only one core gets utilitized at a time.  Tunnel 
fixes this by having multiple event-pump threads -- optimally, one thread 
per core. When a new connection comes in, it is handed off to an event-pump 
thread which multiplexes the new client (along with many others) for the life 
of the connection.

# Compiling and Running

'''bash
# Install build-essential, pthreads, and libevent:
sudo apt-get install build-essential checkinstall \
 libpthread-stubs0 libpthread-stubs0-dev libevent-dev

# Install CyaSSL (due to Launchpad Bug #624840):
cd ./third-party/cyassl-2.9.4/
./configure
make
sudo make install  # Puts it into /usr/local/ by default.
cd ../../

# Build Tunnel:
cd ./src  # into ./tunnel/src/
make
cd ..  # back to ./tunnel

# Edit tunnel.ini to taste. Comments within.
gedit ./tunnel.ini  # Yeah, that's right. Gedit. Punk.

# Finally, run it:
./tunnel
'''

Daemonization is not done yet (it's on the task list). For now it can be
run using your favorite daemonizer, such as 'screen'.

If you need a plaintext server to test against there is a trivial HTTP server
(in Python) in ./tools/. 

'''bash
cd ./tools/
./simple_http_server.sh
'''

# Current Status

Tunnel is not ready for prime time. It's only had a few hours of
work put into it. At this writing, it's only been barely tested with Firefox
and Chrome.

For a commercial-grade solution, check out STunnel.

# Future Work

1. Use libevent logging callback to pipe debug messages to syslog
1. Unit tests (incl. real network testing on a server)
1. daemonize(), incl. PID file
1. SysV init scripts
1. Set loglevel using the config file (tunnel.ini)
1. Full doxygen coverage
1. Static analysis / valgrind
1. Stress/performance testing
1. Packaging / signed binaries

