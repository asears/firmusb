/* Minimal vendored pcap-inttypes.h */
#ifndef pcap_pcap_inttypes_h
#define pcap_pcap_inttypes_h

#if defined(_MSC_VER)
  #if _MSC_VER >= 1800
    #include <inttypes.h>
  #else
    typedef unsigned char uint8_t;
    typedef signed char int8_t;
    typedef unsigned short uint16_t;
    typedef signed short int16_t;
    typedef unsigned int uint32_t;
    typedef signed int int32_t;
    #ifdef _MSC_EXTENSIONS
      typedef unsigned _int64 uint64_t;
      typedef _int64 int64_t;
    #else
      typedef unsigned long long uint64_t;
      typedef long long int64_t;
    #endif
  #endif
#else
  #include <inttypes.h>
#endif

/* Basic legacy BSD-style types used by libpcap headers */
#ifndef __PCAP_BASIC_TYPES_DEFINED
#define __PCAP_BASIC_TYPES_DEFINED
typedef unsigned char u_char;
typedef unsigned int u_int;
typedef unsigned short u_short;
#endif

#endif /* pcap/pcap-inttypes.h */
