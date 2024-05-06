/*
 * File: timesync/timesync.c
 * Author: Hankso <hankso1106@gmail.com>
 * Time: Wed 02 Sep 2020 05:26:39 PM CST
 */

#include "timesync.h"

#include <sys/time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

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
    struct timespec tmp;
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
    struct timespec tmp;
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

static SemaphoreHandle_t lock = NULL;

static void lock_acquire() { if (lock) xSemaphoreTake(lock, portMAX_DELAY); }
static void lock_release() { if (lock) xSemaphoreGive(lock); }

static void log_error(const char *tag, const int sock, const char *msg) {
    if (sock < 0) {
        ESP_LOGE(tag, "%s errno=%d %s", msg ?: "", errno, strerror(errno));
    } else {
        ESP_LOGE(tag, "%s sock=%d errno=%d %s",
                 msg ?: "", sock, errno, strerror(errno));
    }
}

// Convert timesync packtype to bytearray and vice versa
static union d2b_t {
    TIMESYNC_PACKTYPE d;
    char b[sizeof(TIMESYNC_PACKTYPE)];
} ts_convert;

typedef struct {
    double offset; // timestamp offset between server and client
    double sync;   // when this synchronization happen
    double send;   // when sync response is sent
} timesync_result_t;

typedef struct {
    int fd;         // client's file descriptor
    double offset;  // average value: sum(offsets) / count
    double rtript;  // round-trip transfer time
    uint32_t count; // synchronization times counter
    timesync_result_t results[3];   // latest three results
    char addr[INET_ADDRSTRLEN + 6]; // socket remote address
} timesync_client_t;

typedef struct {
    const char *host;
    uint16_t port;
} timesync_addr_t;

const char * getsockaddr(int fd) {
    static char ret[INET_ADDRSTRLEN + 6];
    struct sockaddr_in raddr;
    socklen_t len = sizeof(raddr);
    if (getpeername(fd, (struct sockaddr *)&raddr, &len) < 0) return "unknown";
    sprintf(ret, "%s:%d", inet_ntoa(raddr.sin_addr), ntohs(raddr.sin_port));
    return ret;
}

static const char * TSS = "TSS";
static struct pollfd pollfds[TIMESYNC_CLIENTS_NUM + 1];
static timesync_client_t clients[TIMESYNC_CLIENTS_NUM];
static int server = -1;

void timesync_server_status() {
    bool header = false;
    LOOPN(i, TIMESYNC_CLIENTS_NUM) {
        if (clients[i].fd < 0) continue;
        if (!header) {
            printf("FD Counts %18s %18s %13s\n",
                   "AvrTimeOffset(s)", "SyncTime(s)", "RoundTrip(ms)");
            header = true;
        }
        printf("%2d %06u %18.6f %18.6f %13.3f\n",
               clients[i].fd, clients[i].count, clients[i].offset,
               clients[i].results[(clients[i].count - 1) % 3].sync,
               clients[i].rtript * TIMESTAMP_MS);
        LOOPND(j, MIN(clients[i].count, 3)) {
            uint32_t cnt = clients[i].count - j - 1;
            timesync_result_t *rst = &clients[i].results[cnt % 3];
            printf(" > %06u %18.6f %18.6f\n", cnt + 1, rst->offset, rst->sync);
        }
    }
}

static int timesync_server_find(int fd) {
    LOOPN(i, TIMESYNC_CLIENTS_NUM) { if (clients[i].fd == fd) return i; }
    return -1;
}

/* TS Server Side new client
 *   - setsockopt(NODELAY | SNDTIMEO)
 *   - register to pollfds
 */
static void timesync_server_add(int fd) {
    int nodelay = 1;
    struct timeval timeout = { .tv_sec = 0, .tv_usec = 500 }; // 0.5ms
    size_t tlen = sizeof(timeout), nlen = sizeof(nodelay);
    lock_acquire();
    LOOPN(i, TIMESYNC_CLIENTS_NUM) {
        if (clients[i].fd == fd || clients[i].fd > 0) continue;
        if (
            setsockopt(fd, SOL_SOCKET,  SO_SNDTIMEO, (void *)&timeout, tlen) ||
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (void *)&nodelay, nlen)
        ) break;
        strcpy(clients[i].addr, getsockaddr(fd));
        ESP_LOGI(TSS, "client %s connected", clients[i].addr);
        clients[i].fd = pollfds[i].fd = fd;
        pollfds[i].events = POLLIN;
        fd = 0; // added
        break;
    }
    lock_release();
    if (fd) { // not added
        close(fd);
        ESP_LOGW(TSS, "client %s rejected", getsockaddr(fd));
    }
}

/* TS Server Side close socket
 *   - close() socket
 *   - clean struct pollfd
 *   - set fd to -1
 */
static int timesync_server_close(int *fd) {
    if (*fd < 0) return 1;
    lock_acquire();
    int idx = timesync_server_find(*fd);
    if (idx != -1) pollfds[idx].fd = clients[idx].fd = -1;
    close(*fd);
    *fd = -1;
    lock_release();
    return 0;
}

/* TS Server Side initialization
 *   - create socket()
 *   - bind() as server
 *   - listen() for clients
 *   - prepare for poll()
 */
int timesync_server_init(uint16_t port) {
#ifdef TIMESYNC_THREAD_SAFE
    if (!lock) lock = xSemaphoreCreateBinary();
#endif
    if (server >= 0) return 0;
    ESP_LOGD(TSS, "TimeSync Server init");
    if (( server = socket(AF_INET, SOCK_STREAM, IPPROTO_IP) ) == -1) {
        log_error(TSS, server, "socket");
        return -1;
    }
    LOOPN(i, TIMESYNC_CLIENTS_NUM) { clients[i].fd = pollfds[i].fd = -1; }
    pollfds[TIMESYNC_CLIENTS_NUM].events = POLLIN;
    pollfds[TIMESYNC_CLIENTS_NUM].fd = server;
    struct sockaddr_in laddr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr = { .s_addr = htonl(INADDR_ANY) }
    };
    if (bind(server, (struct sockaddr *)&laddr, sizeof(laddr))) {
        log_error(TSS, server, "bind");
    } else if (listen(server, 1)) {
        log_error(TSS, server, "listen");
    } else {
        ESP_LOGI(TSS, "listening on port %d", ntohs(laddr.sin_port));
        return 0;
    }
    timesync_server_close(&server);
    return -1;
}

/* TS Server Side protocol
 *   - wait for timeinit
 *   - reply with timesync
 *   - valid on timedone
 *   - calculate time offset
 */
static int timesync_server_handle(
    timesync_client_t *client, char *rcvbuf, size_t msglen, double ts_recv
) {
    static size_t packlen = sizeof(TIMESYNC_PACKTYPE);
    static char sndbuf[8 + sizeof(TIMESYNC_PACKTYPE)] = "timesync";
    timesync_result_t *result;

    lock_acquire();
    // parse message according to TimeSync Protocol
    if (!strncmp("timeinit", rcvbuf, 8)) {
        double ts_send = get_timestamp(0), ts_sync = (ts_recv + ts_send) / 2;
        ts_convert.d = ts_sync;
        memcpy(sndbuf + 8, ts_convert.b, packlen);
        // respond to client as early as possible after ts_send recorded
        if (write(client->fd, sndbuf, 8 + packlen) != -1) {
            // save info for next loop
            result = &client->results[client->count++ % 3];
            result->sync = ts_sync;
            result->send = ts_send;
        }
    } else if (
        !strncmp("timedone", rcvbuf, 8)
        && (msglen >= (8 + 2 * packlen))
        && client->count
    ) {
        memcpy(ts_convert.b, rcvbuf + 8          , packlen);
        double ts_client_sync   = ts_convert.d;
        memcpy(ts_convert.b, rcvbuf + 8 + packlen, packlen);
        double ts_client_offset = ts_convert.d;
        result = &client->results[(client->count - 1) % 3];
        result->offset = (ts_recv + result->send) / 2 - ts_client_sync;
        // values calculated by server and client should approximately equal
        if (ABS(result->offset - ts_client_offset) > 0.1) {
            ESP_LOGW(TSS, "%s unmatched offset. S: %f, C: %f",
                    client->addr, result->offset, ts_client_offset);
            result->offset = 0;
        } else if (client->count == 1) {
            client->offset = result->offset;
            client->rtript = ts_recv - result->send;
        } else {
            client->offset = (client->offset + result->offset) / 2;
            client->rtript = (client->rtript + ts_recv - result->send) / 2;
        }
    } else {
        rcvbuf[msglen] = '\0';
        ESP_LOGI(TSS, "client %s message: `%s`", client->addr, rcvbuf);
    }
    lock_release();
    return 0;
}

/* TS Server Side mainloop:
 *   - poll on server socket and client sockets
 *   - handle new clients
 *   - handle client's message
 *   - handle when clients leave
 */
int timesync_server_loop(uint16_t ms) {
    static char rcvbuf[TIMESYNC_RCVBUF_SIZE + 1];
    int rc, fd, idx, msglen;

    if (server < 0) {
        msleep(ms);
        return -1;
    }
    if (( rc = poll(pollfds, TIMESYNC_CLIENTS_NUM + 1, ms) ) < 0) {
        log_error(TSS, server, "poll");
        return -1;
    } else if (!rc) {
        return 0; // timeout
    }

    double ts_recv = get_timestamp(0); // right after poll returned

    LOOPN(i, TIMESYNC_CLIENTS_NUM + 1) {
        if (!pollfds[i].revents) continue;
        if (( fd = pollfds[i].fd ) == server) {
            if (( fd = accept(server, NULL, NULL) ) < 0) {
                log_error(TSS, server, "accept");
                return 1;
            } else {
                timesync_server_add(fd);
                continue;
            }
        }
        if (( idx = timesync_server_find(fd) ) < 0) {
            log_error(TSS, fd, "find"); // this should not happen
            timesync_server_close(&fd);
            continue;
        }
        if (( msglen = recv(fd, rcvbuf, TIMESYNC_RCVBUF_SIZE, 0) ) <= 0) {
            ESP_LOGI(TSS, "client %s closed", clients[i].addr);
            timesync_server_close(&clients[idx].fd);
        } else if (msglen < 8) {
            rcvbuf[msglen] = '\0';
            ESP_LOGW(TSS, "client %s message: `%s`", clients[i].addr, rcvbuf);
        } else {
            timesync_server_handle(&clients[idx], rcvbuf, msglen, ts_recv);
        }
    }
    return 0;
}

int timesync_server_exit() {
    if (timesync_server_close(&server)) return 1;
    lock_acquire();
    LOOPN(i, TIMESYNC_CLIENTS_NUM) {
        if (clients[i].fd < 0) continue;
        shutdown(clients[i].fd, SHUT_RDWR);
        close(clients[i].fd);
    }
    lock_release();
    ESP_LOGD(TSS, "TimeSync Server exit");
    return 0;
}

static const char *TSC = "TSC";
static timesync_client_t client = { 0 };

/* TS Client Side initialization
 *   - create socket()
 *   - setsockopt(RCVTIMEO)
 *   - connect()
 */
int timesync_client_init(const char *host, uint16_t port) {
    if (client.fd == 0) client.fd = -1;
    if (client.fd >= 0) return 0;
    ESP_LOGD(TSC, "TimeSync Client init");
    char address[INET_ADDRSTRLEN];
    struct sockaddr_in raddr = {
        .sin_family = AF_INET,
        .sin_port = htons(port)
    };
    if (inet_pton(AF_INET, host, &raddr.sin_addr) != 1) {
        log_error(TSC, client.fd, "inet_pton");
        return -1;
    }
    if (!inet_ntop(AF_INET, &(raddr.sin_addr), address, sizeof(address))) {
        log_error(TSC, client.fd, "inet_ntop");
        return -1;
    }
    if (( client.fd = socket(AF_INET, SOCK_STREAM, 0) ) < 0) {
        log_error(TSC, client.fd, "socket");
        return -1;
    }
    struct timeval
        rcvto = { .tv_sec = 1, .tv_usec = 0 },
        sndto = { .tv_sec = 3, .tv_usec = 0 };
    size_t tlen = sizeof(struct timeval);
    if (
        setsockopt(client.fd, SOL_SOCKET, SO_SNDTIMEO, (void *)&sndto, tlen) ||
        setsockopt(client.fd, SOL_SOCKET, SO_RCVTIMEO, (void *)&rcvto, tlen)
    ) {
        log_error(TSC, client.fd, "setsockopt");
    } else if (connect(client.fd, (struct sockaddr *)&raddr, sizeof(raddr))) {
        log_error(TSC, client.fd, "connect");
    } else {
        strcpy(client.addr, getsockaddr(client.fd));
        ESP_LOGI(TSC, "using server %s", client.addr);
        return 0;
    }
    timesync_client_exit();
    return -1;
}

/* TS Client Side protocol
 *   - send timeinit
 *   - wait for timesync
 *   - send optional timedone optional
 *   - calculate time offset
 */
int timesync_client_sync(double *offset, uint8_t ack) {
    static int packlen = sizeof(TIMESYNC_PACKTYPE);
    static char rcvbuf[TIMESYNC_RCVBUF_SIZE];
    static char sndbuf[8 + sizeof(TIMESYNC_PACKTYPE)] = "timeinit";
    static char ackbuf[8 + sizeof(TIMESYNC_PACKTYPE) * 2] = "timedone";
    if (client.fd < 0) return -1;
    // init timesync request
    double ts_send = get_timestamp(0);
    ts_convert.d = ts_send;
    memcpy(sndbuf + 8, ts_convert.b, packlen);
    if (write(client.fd, sndbuf, 8 + packlen) == -1) {
        log_error(TSC, client.fd, "timesync server hanged");
        return -1;
    }
    // wait for timesync response
    int msglen = read(client.fd, rcvbuf, TIMESYNC_RCVBUF_SIZE - 1);
    double ts_recv = get_timestamp(0);
    // recv timeout
    if (msglen < 0) return 1;
    if (msglen < 8 || strncmp(rcvbuf, "timesync", 8)) {
        rcvbuf[msglen] = '\0';
        ESP_LOGI(TSC, "server message: `%s`", rcvbuf);
        return 1;
    }
    // parse response to calculate timesync offset
    memcpy(ts_convert.b, rcvbuf + 8, packlen);
    double ts_server_sync = ts_convert.d;
    timesync_result_t *result = &client.results[client.count++ % 3];
    result->sync = (ts_recv + ts_send) / 2;
    result->offset = ts_server_sync - result->sync;
    if (client.count == 1) {
        client.offset = result->offset;
        client.rtript = ts_recv - ts_send;
    } else {
        client.offset = (client.offset + result->offset) / 2;
        client.rtript = (client.rtript + ts_recv - ts_send) / 2;
    }
    if (offset) *offset = client.offset;
    if (!ack) return result->send = 0;
    result->send = get_timestamp(0);
    ts_convert.d = result->offset;
    memcpy(ackbuf + 8          , ts_convert.b, packlen);
    ts_convert.d = result->send;
    memcpy(ackbuf + 8 + packlen, ts_convert.b, packlen);
    if (write(client.fd, ackbuf, 8 + packlen * 2) == -1) {
        log_error(TSC, client.fd, "timesync server hanged");
        return -1;
    }
    return 0;
}

int timesync_client_xsync(double *offset, uint8_t iters) {
    LOOPN(i, iters - 1) {
        int rc = timesync_client_sync(offset, 0);
        if (rc == -1) return rc;
        if (rc == 0) msleep((rand() % 50) + 50);  // 50 - 100 ms
    }
    return timesync_client_sync(offset, 1);
}

int timesync_client_exit() {
    if (client.fd < 0) return 1;
    close(client.fd);
    client.fd = -1;
    ESP_LOGD(TSC, "TimeSync Client exit");
    return 0;
}
