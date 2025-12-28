/* Vendored from https://github.com/the-tcpdump-group/libpcap
 * See third_party/libpcap/LICENSE for license details.
 * Original file: pcap/pcap.h
 */

/* -*- Mode: c; tab-width: 8; indent-tabs-mode: 1; c-basic-offset: 8; -*- */
/*
 * Copyright (c) 1993, 1994, 1995, 1996, 1997
 *    The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by the Computer Systems
 *    Engineering Group at Lawrence Berkeley Laboratory.
 *
 * 4. Neither the name of the University nor of the Laboratory may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lib_pcap_pcap_h
#define lib_pcap_pcap_h

#include <pcap/funcattrs.h>
#include <pcap/pcap-inttypes.h>

#if defined(_WIN32)
  #include <winsock2.h>  /* u_int, u_char etc. */
  #include <io.h>        /* _get_osfhandle() */
#else
  #include <sys/types.h> /* u_int, u_char etc. */
  #include <sys/time.h>
#endif

#include <pcap/socket.h>  /* for PCAP_SOCKET, as the active-mode rpcap APIs use it */

#ifndef PCAP_DONT_INCLUDE_PCAP_BPF_H
#include <pcap/bpf.h>
#endif

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PCAP_VERSION_MAJOR 2
#define PCAP_VERSION_MINOR 4

#define PCAP_ERRBUF_SIZE 256

/* Provide minimal compatibility macros if upstream annotation macros are missing */
#ifndef PCAP_AVAILABLE_0_4
#define PCAP_AVAILABLE_0_4
#endif
#ifndef PCAP_AVAILABLE_0_6
#define PCAP_AVAILABLE_0_6
#endif
#ifndef PCAP_AVAILABLE_0_7
#define PCAP_AVAILABLE_0_7
#endif
#ifndef PCAP_AVAILABLE_0_8
#define PCAP_AVAILABLE_0_8
#endif
#ifndef PCAP_AVAILABLE_0_9
#define PCAP_AVAILABLE_0_9
#endif
#ifndef PCAP_AVAILABLE_1_0
#define PCAP_AVAILABLE_1_0
#endif
#ifndef PCAP_WARN_UNUSED_RESULT
#define PCAP_WARN_UNUSED_RESULT
#endif
#ifndef PCAP_NONNULL
#define PCAP_NONNULL(...)
#endif
#ifndef PCAP_DEPRECATED
#define PCAP_DEPRECATED(msg)
#endif

#ifndef PCAP_NETMASK_UNKNOWN
#define PCAP_NETMASK_UNKNOWN 0xffffffff
#endif

/* Minimal type and function declarations used by monitor_dns */
typedef struct pcap pcap_t;
typedef struct pcap_dumper pcap_dumper_t;
typedef struct pcap_if pcap_if_t;
typedef struct pcap_addr pcap_addr_t;

struct pcap_pkthdr {
    struct timeval ts; /* time stamp */
    bpf_u_int32 caplen; /* length of portion present in data */
    bpf_u_int32 len;    /* length of this packet prior to any slicing */
};

struct pcap_addr {
    struct pcap_addr *next;
    struct sockaddr *addr;
    struct sockaddr *netmask;
    struct sockaddr *broadaddr;
    struct sockaddr *dstaddr;
};

struct pcap_if {
    struct pcap_if *next;
    char *name;
    char *description;
    struct pcap_addr *addresses;
    bpf_u_int32 flags;
};

typedef void (*pcap_handler)(u_char *, const struct pcap_pkthdr *, const u_char *);

PCAP_AVAILABLE_0_4
PCAP_API pcap_t *pcap_open_live(const char *, int, int, int, char *) PCAP_NONNULL(5);

PCAP_AVAILABLE_0_7
PCAP_API int pcap_findalldevs(pcap_if_t **, char *) PCAP_NONNULL(2) PCAP_WARN_UNUSED_RESULT;

PCAP_AVAILABLE_0_7
PCAP_API void pcap_freealldevs(pcap_if_t *);

PCAP_AVAILABLE_0_4
PCAP_API int pcap_compile(pcap_t *, struct bpf_program *, const char *, int, bpf_u_int32) PCAP_WARN_UNUSED_RESULT;

PCAP_AVAILABLE_0_4
PCAP_API int pcap_setfilter(pcap_t *, struct bpf_program *) PCAP_WARN_UNUSED_RESULT;

PCAP_AVAILABLE_0_6
PCAP_API void pcap_freecode(struct bpf_program *);

PCAP_AVAILABLE_0_4
PCAP_API int pcap_loop(pcap_t *, int, pcap_handler, u_char *) PCAP_WARN_UNUSED_RESULT;

PCAP_AVAILABLE_0_4
PCAP_API void pcap_close(pcap_t *);

PCAP_AVAILABLE_0_4
PCAP_API const u_char *pcap_next(pcap_t *, struct pcap_pkthdr *);

PCAP_AVAILABLE_0_4
PCAP_API int pcap_next_ex(pcap_t *, struct pcap_pkthdr **, const u_char **);

/* A number of additional functions, types and macros are defined in the upstream header. */

#ifdef __cplusplus
}
#endif

#endif /* lib_pcap_pcap_h */
