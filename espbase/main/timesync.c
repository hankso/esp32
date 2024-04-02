/*
 * File: timesync/timesync.c
 * Author: Hankso <hankso1106@gmail.com>
 * Time: Wed 02 Sep 2020 05:26:39 PM CST
 */

#include "timesync.h"
#include <sys/time.h>

/* Time Sync timestamp helper functions */

struct timespec * get_systime() {
    static struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return &ts;
}

struct timeval * get_systime_us() {
    static struct timeval ts;
    gettimeofday(&ts, NULL);
    return &ts;
}

double get_timestamp(struct timespec *ts) {
    ts = (ts != NULL) ? ts : get_systime();
    return ts->tv_sec + (double)ts->tv_nsec / TIMESTAMP_NS; // nanosecond
}

double get_timestamp_us(struct timeval *ts) {
    ts = (ts != NULL) ? ts : get_systime_us();
    return ts->tv_sec + (double)ts->tv_usec / TIMESTAMP_US; // microsecond
}

const char * format_datetime(struct timespec *ts) {
    static char buf[18]; // xxxx-xx-xx-xxxxxx\0
    ts = (ts != NULL) ? ts : get_systime();
    strftime(buf, sizeof(buf), "%F-%H%M%S", localtime(&ts->tv_sec));
    return buf;
}

const char * format_datetime_us(struct timeval *ts) {
    static struct timespec tmp;
    ts = (ts != NULL) ? ts : get_systime_us();
    tmp.tv_sec = ts->tv_sec;
    tmp.tv_nsec = ts->tv_usec * TIMESTAMP_U2N;
    return format_datetime(&tmp);
}

const char * format_timestamp(struct timespec *ts) {
    static char buf[13]; // xx:xx:xx.xxx\0
    ts = (ts != NULL) ? ts : get_systime();
    strftime(buf, 9, "%T", localtime(&ts->tv_sec));
    uint16_t ms = ts->tv_nsec / TIMESTAMP_US;
    snprintf(buf + 8, 5, ".%03d", ms % 1000);
    return buf;
}

const char * format_timestamp_us(struct timeval *ts) {
    static struct timespec tmp;
    ts = (ts != NULL) ? ts : get_systime_us();
    tmp.tv_sec = ts->tv_sec;
    tmp.tv_nsec = ts->tv_usec * TIMESTAMP_U2N;
    return format_timestamp(&tmp);
}

double set_timestamp(struct timespec *ts, double timestamp) {
    if (!timestamp)
        timestamp = get_timestamp(0);
    if (ts != NULL) {
        ts->tv_sec = (time_t)timestamp;
        ts->tv_nsec = (timestamp - ts->tv_sec) * TIMESTAMP_NS;
    }
    return timestamp;
}

double set_timestamp_us(struct timeval *ts, double timestamp) {
    if (!timestamp)
        timestamp = get_timestamp(0);
    if (ts != NULL) {
        ts->tv_sec = (time_t)timestamp;
        ts->tv_usec = (timestamp - ts->tv_sec) * TIMESTAMP_US;
    }
    return timestamp;
}

struct timespec * get_timeout(uint32_t ms, struct timespec *tout) {
    struct timeval *now = get_systime_us();
    uint64_t nsec = (ms % TIMESTAMP_MS) * TIMESTAMP_M2N + now->tv_usec * TIMESTAMP_U2N;
    if (tout) {
        tout->tv_sec = now->tv_sec + ms / TIMESTAMP_MS + nsec / TIMESTAMP_NS;
        tout->tv_nsec = nsec % TIMESTAMP_NS;
    }
    return tout;
}

struct timespec * get_timeout_alignup(uint32_t ns, struct timespec *tout) {
    struct timeval *now = get_systime_us();
    uint64_t nsec = now->tv_sec * TIMESTAMP_NS + now->tv_usec * TIMESTAMP_U2N;
    nsec += ns - (nsec & ns);
    if (tout) {
        tout->tv_sec = nsec / TIMESTAMP_NS;
        tout->tv_nsec = nsec % TIMESTAMP_NS;
    }
    return tout;
}

/* Time Sync Server & Client implementation
 *
 * This is a lightweight time syncing protocol based on TCP connection.
 *
 * See more about specification at https://github.com/hankso/timesync
 */

#if 0
#ifdef TIMESYNC_THREAD_SAFE

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void lock_acquire() { pthread_mutex_lock(&lock); }
void lock_release() { pthread_mutex_unlock(&lock); }

#else

void lock_acquire() {}
void lock_release() {}

#endif // TIMESYNC_THREAD_SAFE

typedef struct {
    double offset; // timestamp offset between server and client
    double sync;   // when this synchronization happen
    double send;   // when sync response is sent
} timesync_result_t;

typedef struct {
    timesync_result_t results[3];   // latest three results
    uint32_t count; // synchronization timescounter
    double offset;  // average value: sum(offsets) / count
    double rtript;  // round-trip transfer time
    int fd;         // client's file descriptor
} timesync_client_t;

typedef struct {
    const char *host;
    uint16_t port;
} timesync_addr_t;

static int server = 0;
static epoll_t poller = epoll_failed;
static timesync_client_t *clients;
static const uint16_t nclient = TIMESYNC_CLIENTS_NUM;

static int close_socket(int *fd) {
    if (*fd <= 0) return 1;
    close(*fd);
    lock_acquire();
    for (uint8_t i = 0; clients && i < nclient; i++) {
        if (clients[i].fd != *fd) continue;
        epoll_ctl(poller, EPOLL_CTL_DEL, *fd, NULL);
        memset(&clients[i], 0, sizeof(timesync_client_t));
    }
    lock_release();
    *fd = 0;
    return 0;
}

const char * getsockaddr(int fd) {
    static struct sockaddr_in raddr;
    static socklen_t raddrlen = sizeof(raddr);
    static char ret[INET_ADDRSTRLEN + 6];
    if ( getpeername(fd, (struct sockaddr *)&raddr, &raddrlen) == -1 ) return "unknown";
    snprintf(ret, INET_ADDRSTRLEN + 6, "%s:%d", inet_ntoa(raddr.sin_addr), ntohs(raddr.sin_port));
    return ret;
}

void clients_info(int fd, int verbose) {
    puts("FD Counts   AvrTimeOffset(s) RoundTrip(ms)       SyncTime(s)");
    for (uint8_t i = 0; i < nclient; i++) {
        if (fd != -1 && clients[i].fd != fd) continue;
        if (clients[i].fd <= 0) {
            puts("-- Client not using");
        } else {
            printf("%2d %6u %18.6f %13.3f %18.6f\n",
                   clients[i].fd, clients[i].count,
                   clients[i].offset, clients[i].rtript * TIMESTAMP_MS,
                   clients[i].results[(clients[i].count - 1) % 3].sync);
        }
        if (verbose) {
            uint8_t nresult = clients[i].count < 3 ? clients[i].count : 3;
            while (nresult--) {
                uint32_t count = clients[i].count - nresult - 1;
                timesync_result_t *rst = &clients[i].results[count % 3];
                printf(" > ID %3u %18.6f %13s %18.6f\n",
                       count + 1, rst->offset, "", rst->sync);
            }
        }
    }
}

// Convert double to bytearray and vice versa
static union d2b_t {
    TIMESYNC_PACKTYPE d;
    char b[sizeof(TIMESYNC_PACKTYPE)];
} ts_convert;

/* TS Server Side initialization: socket() => bind() => listen() => epoll() */
int timesync_server_init(uint16_t port) {
    ASSERT_WINSOCK();
    puts("[TSS] init TimeSync Server");
    if (!( clients = (timesync_client_t *)calloc(nclient, sizeof(timesync_client_t)) )) {
        perror("[TSS] calloc");
        return -1;
    }
    if (( server = socket(AF_INET, SOCK_STREAM, 0) ) == -1) {
        perror("[TSS] socket");
        free(clients);
        clients = NULL;
        return -1;
    }
    struct sockaddr_in laddr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr = { .s_addr = htonl(INADDR_ANY) }
    };
    struct epoll_event ev = {
        .events = EPOLLIN,
        .data = { .fd = server }
    };
    printf("[TSS] listening on port %d\n", ntohs(laddr.sin_port));
    if        (bind(server, (struct sockaddr *)&laddr, sizeof(laddr))) {
        perror("[TSS] bind");
    } else if (listen(server, 1)) {
        perror("[TSS] listen");
    } else if (( poller = epoll_create(nclient + 1) ) == epoll_failed) {
        perror("[TSS] epoll_create");
    } else if (epoll_ctl(poller, EPOLL_CTL_ADD, server, &ev)) {
        perror("[TSS] epoll_ctl_add");
    } else return 0;
    close_socket(&server);
    free(clients);
    clients = NULL;
    return -1;
}

/* TS Server Side new client: setsockopt(NODELAY | SNDTIMEO) => epoll_ctl() => welcome() */
static void timesync_server_addclient(int fd) {
    static int nodelay = 1;
    static struct timeval timeout = { .tv_sec = 0, .tv_usec = 500 };  // default 0.5ms
    static size_t tlen = sizeof(timeout), nlen = sizeof(nodelay);
    struct epoll_event ev = { .events = EPOLLIN, .data = { .fd = fd } };
    lock_acquire();
    for (uint8_t i = 0; clients && i < nclient; i++) {
        if (clients[i].fd == fd || clients[i].fd > 0)
            continue;  // space already occupied
        if (
            setsockopt(fd, SOL_SOCKET,  SO_SNDTIMEO, (void *)&timeout, tlen) ||
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (void *)&nodelay, nlen) ||
            epoll_ctl(poller, EPOLL_CTL_ADD, fd, &ev)
        ) break;
        printf("[TSS] client %s connected\n", getsockaddr(fd));
        clients[i].fd = fd;
        fd = 0;
        break;
    }
    lock_release();
    if (fd) {
        close(fd);
        printf("[TSS] client %s rejected\n", getsockaddr(fd));
    }
}

/* TS Server Side protocol: timesync => timeack(check timedone) */
static int timesync_server_handle(timesync_client_t *client,
                                  char *rcvbuf, size_t msglen,
                                  double ts_recv)
{
    static size_t packlen = sizeof(TIMESYNC_PACKTYPE);
    static char sndbuf[8 + sizeof(TIMESYNC_PACKTYPE)] = {
        't', 'i', 'm', 'e', 's', 'y', 'n', 'c', 0
    };
    const char *raddrs = getsockaddr(client->fd);
    timesync_result_t *result;

    lock_acquire();
    rcvbuf[msglen] = '\0';
    // parse message according to TimeSync Protocol
    if (!strncmp("timeinit", rcvbuf, 8)) {
        double ts_send = get_timestamp(0), ts_sync = (ts_recv + ts_send) / 2;
        ts_convert.d = ts_sync; memcpy(sndbuf + 8, ts_convert.b, packlen);
        // respond to client as early as possible after ts_send recorded
        if (write(client->fd, sndbuf, 8 + packlen) != -1) {
            // save info for next loop
            result = &client->results[client->count++ % 3];
            result->sync = ts_sync;
            result->send = ts_send;
        }
    } else if (
        !strncmp("timedone", rcvbuf, 8) &&
        (msglen >= (8 + 2 * packlen)) && client->count
    ) {
        memcpy(ts_convert.b, rcvbuf + 8          , packlen);
        double ts_client_sync   = ts_convert.d;
        memcpy(ts_convert.b, rcvbuf + 8 + packlen, packlen);
        double ts_client_offset = ts_convert.d;
        result = &client->results[(client->count - 1) % 3];
        result->offset = (ts_recv + result->send) / 2 - ts_client_sync;
        // Validation of time offset:
        //    values calculated by server and client should approximately equal
        if (ABS(result->offset - ts_client_offset) > 0.1) {
            printf("[TSS] %s unmatched offset. S: %f, C: %f\n",
                    raddrs, result->offset, ts_client_offset);
            result->offset = 0;
        } else if (client->count == 1) {
            client->offset = result->offset;
            client->rtript = ts_recv - result->send;
        } else {
            client->offset = (client->offset + result->offset) / 2;
            client->rtript = (client->rtript + ts_recv - result->send) / 2;
        }
    } else {
        printf("[TSS] client %s message: `%s`\n", raddrs, rcvbuf);
    }
    lock_release();
    return 0;
}

/* TS Server Side mainloop:
 *   - Poll on server socket and client sockets
 *   - Handle new clients
 *   - Handle client's message
 *   - Handle when clients leave
 */
int timesync_server_loop(uint16_t ms) {
    static struct epoll_event events[TIMESYNC_CLIENTS_NUM + 2];
    static const uint8_t nevent = TIMESYNC_CLIENTS_NUM + 2;
    static char rcvbuf[TIMESYNC_RCVBUF_SIZE + 1];
    int fd, idx, nfds, error = 0, msglen;

    if (poller == epoll_failed || server <= 0)
        return msleep(ms);
    if (( nfds = epoll_wait(poller, events, nevent, ms) ) <= 0)
        return -1;

    double ts_recv = get_timestamp(0);
    for (int i = 0; !error && i < nfds; i++) {
        if (( fd = events[i].data.fd ) == server) {
            if (( fd = accept(server, NULL, NULL) ))
                timesync_server_addclient(fd);
            continue;
        }
        idx = clients ? 0 : nclient;
        while (idx < nclient && fd != clients[idx].fd) idx++;
        if (idx == nclient) { // fd not found in clients list
            printf("[TSS] recv %s message: `%s`\n", getsockaddr(fd), rcvbuf);
            continue;
        }
        if (( msglen = recv(fd, (char *)rcvbuf, TIMESYNC_RCVBUF_SIZE, 0) ) < 0) {
            error = errno;
        } else if (msglen) {
            timesync_server_handle(&clients[idx], rcvbuf, msglen, ts_recv);
        } else {
            printf("[TSS] client %s closed\n", getsockaddr(fd));
            close_socket(&fd);
        }
    }
    return error;
}

int timesync_server_exit() {
    if (server <= 0) return 1;
    lock_acquire();
    for (int i = 0, fd; clients && i < nclient; i++) {
        if (( fd = clients[i].fd ) <= 0) continue;
        if ( epoll_ctl(poller, EPOLL_CTL_DEL, fd, NULL) == -1 )
            perror("epoll_ctl del");
        shutdown(fd, SHUT_RDWR); close(fd);
    }
    free(clients);
    clients = NULL;
    server = close(server);
    puts("[TSS] exited");
    lock_release();
    return 0;
}

static timesync_client_t client;

/* TS Client Side initialization: socket() => setsockopt(RCVTIMEO) => connect() */
int timesync_client_init(const char *host, uint16_t port) {
    ASSERT_WINSOCK();
    puts("[TSC] init TimeSync Client");
    int addrlen = INET_ADDRSTRLEN; char address[INET_ADDRSTRLEN];
    struct sockaddr_in raddr = { .sin_family = AF_INET, .sin_port = htons(port) };
    struct timeval rcvto = { .tv_sec = 1, .tv_usec = 0 }, sndto = { .tv_sec = 3, .tv_usec = 0 };
    // validation of host ip address
    if ( inet_pton(AF_INET, host, &raddr.sin_addr) != 1 ) { puts("inet_pton: Invalid ipaddress"); return 1; }
    if ( inet_ntop(AF_INET, &(raddr.sin_addr), address, addrlen) == NULL ) { perror("inet_ntop"); return 1; }
    printf("[TSC] using server %s:%d\n", address, port);
    if ( (client.fd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) { perror("socket"); return 1; }
    if ( setsockopt(client.fd, SOL_SOCKET, SO_SNDTIMEO, (void *)&sndto, sizeof(sndto)) ) goto error;
    if ( setsockopt(client.fd, SOL_SOCKET, SO_RCVTIMEO, (void *)&rcvto, sizeof(rcvto)) ) goto error;
    if ( connect(client.fd, (struct sockaddr *)&raddr, sizeof(raddr)) ) { perror("connect"); goto error; }
    return 0;
error:
    timesync_client_exit();
    return 1;
}

/* TS Client Side protocol: timeinit => timedone(optional) => timeoffset */
int timesync_client_sync(uint8_t ack) {
    static timesync_result_t *result;
    static int msglen = 0, packlen = sizeof(TIMESYNC_PACKTYPE);
    static char rcvbuf[TIMESYNC_RCVBUF_SIZE];
    static char sndbuf[8 + sizeof(TIMESYNC_PACKTYPE)] = {
        't', 'i', 'm', 'e', 'i', 'n', 'i', 't', 0
    };
    static char ackbuf[8 + sizeof(TIMESYNC_PACKTYPE) * 2] = {
        't', 'i', 'm', 'e', 'd', 'o', 'n', 'e', 0
    };
    if (client.fd <= 0) return 1;
    // init timesync request
    double ts_send = get_timestamp(0);
    ts_convert.d = ts_send; memcpy(sndbuf + 8, ts_convert.b, packlen);
    if (write(client.fd, sndbuf, 8 + packlen) == -1) {
        perror("timesync server hanged");
        return 1;
    }
    // wait for timesync response
    memset(rcvbuf, 0, msglen > 0 ? msglen : 0);
    msglen = read(client.fd, rcvbuf, TIMESYNC_RCVBUF_SIZE - 1);
    double ts_recv = get_timestamp(0);
    // recv timeout
    if (msglen < 0) {
        return 1;
    } else {
        rcvbuf[msglen] = '\0';
    }
    // parse response to calculate timesync offset
    if (!strncmp(rcvbuf, "timesync", 8)) {
        memcpy(ts_convert.b, rcvbuf + 8, packlen); double ts_server_sync = ts_convert.d;
        result = &client.results[client.count++ % 3];
        result->sync = (ts_recv + ts_send) / 2;
        result->offset = ts_server_sync - result->sync;
        if (client.count == 1) {
            client.offset = result->offset;
            client.rtript = ts_recv - ts_send;
        } else {
            client.offset = (client.offset + result->offset) / 2;
            client.rtript = (client.rtript + ts_recv - ts_send) / 2;
        }
        if (!ack) {
            result->send = 0;
        } else {
            result->send = get_timestamp(0);
            ts_convert.d = result->offset; memcpy(ackbuf + 8          , ts_convert.b, packlen);
            ts_convert.d = result->send;   memcpy(ackbuf + 8 + packlen, ts_convert.b, packlen);
            if (write(client.fd, ackbuf, 8 + packlen * 2) == -1) { ; }
        }
    } else {
        printf("[TSC] server message: `%s`\n", rcvbuf);
    }
    return 0;
}

double timesync_client_xsync(uint8_t iters) {
    if (client.fd <= 0) return 0;
    double avr = 0;
    while (iters-- > 0) {
        if (timesync_client_sync(0) != 0) continue;
        avr = avr ? (avr + client.offset) / 2 : client.offset;
        msleep((rand() % 250) + 250);  // 250 - 500 ms
    }
    timesync_client_sync(1);
    return avr;
}

int timesync_client_exit() {
    if (client.fd <= 0) return 1;
    client.fd = close(client.fd);
    puts("[TSC] exited");
    return 0;
}
#endif
