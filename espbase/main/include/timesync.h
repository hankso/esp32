/*
 * File: timesync/timesync.h
 * Author: Hankso <hankso1106@gmail.com>
 * Time: Tue 15 Sep 2020 04:11:21 PM CST
 */

#pragma once

#include "globals.h"

#include <sys/time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#define TIMESTAMP_MS            1000
#define TIMESTAMP_US            1000000
#define TIMESTAMP_NS            1000000000
#define TIMESTAMP_M2U           1000
#define TIMESTAMP_M2N           1000000
#define TIMESTAMP_U2N           1000

#define TIMESYNC_PORT           1918
#define TIMESYNC_RCVBUF_SIZE    128
#define TIMESYNC_CLIENTS_NUM    3
#define TIMESYNC_PACKTYPE       double

#ifdef __cplusplus
extern "C" {
#endif

// Get system time by `clock_gettime`.
// CLOCK_MONOTONIC_RAW is a clock that cannot be set and represents monotonic
// time since some unspecified starting point. This clock is not affected by
// discontinuous jumps (manually changes the clock, by adjtime(3) and NTP).
// It provides access to a raw hardware-based time.
const struct timespec * get_systime();

// Get system time by `gettimeofday`.
const struct timeval * get_systime_us();

// Parse timespec and convert it to timetamp in seconds.
// Use system time if argument ts is NULL/0.
double get_timestamp(const struct timespec *ts);
double get_timestamp_us(const struct timeval *ts);

// Fill timestamp in variable ts (can be timespec or timeval)
// Use system time if argument time is 0.
// Return timestamp filled in ts
double set_timestamp(struct timespec *ts, double time);
double set_timestamp_us(struct timeval *ts, double time);

// strftime with default format: %H:%M:%S.%msec
const char * format_timestamp(const struct timespec *ts);
const char * format_timestamp_us(const struct timeval *ts);
// strftime with default format: %Y-%d-%m-%H%M%S
const char * format_datetime(const struct timespec *ts);
const char * format_datetime_us(const struct timeval *ts);

// Get absolute timestamp with specified timeout offset
struct timespec * get_timeout(uint32_t ms, struct timespec *tout);
struct timespec * get_timeout_alignup(uint32_t ns, struct timespec *tout);

#ifdef CONFIG_LWIP_IPV6
#   define IPV6
#   define ADDRSTRLEN (INET6_ADDRSTRLEN + 6) // 1 for ':' and 5 for 0-65535
#else
#   define ADDRSTRLEN (INET_ADDRSTRLEN + 6)
#endif

// Format ipaddress like xxx.xxx.xxx.xxx:xxxxx
// local=true for getsockname and local=false for getpeername
const char * getaddrname(int fd, bool local);

// TimeSync Server
int timesync_server_init(uint16_t port);
int timesync_server_loop(uint16_t timeout);  // timeout in milliseconds
int timesync_server_exit();
void timesync_server_status();

// TimeSync Client
int timesync_client_init(const char *host, uint16_t port);
int timesync_client_sync(double *tsoffset, uint8_t ack);
int timesync_client_xsync(double *tsoffset, uint8_t iters);
int timesync_client_exit();

#ifdef __cplusplus
}
#endif
