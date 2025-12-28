/* Vendored from https://github.com/the-tcpdump-group/libpcap
 * See third_party/libpcap/LICENSE for license details.
 * Original file: pcap/funcattrs.h
 */

/* -*- Mode: c; tab-width: 8; indent-tabs-mode: 1; c-basic-offset: 8; -*- */
#ifndef lib_pcap_funcattrs_h
#define lib_pcap_funcattrs_h

#include <pcap/compiler-tests.h>

/* Attributes to apply to functions and their arguments, using various compiler-specific extensions. */

#if defined(_WIN32)
  #if defined(pcap_EXPORTS)
    #define PCAP_API_DEF __declspec(dllexport)
  #elif defined(PCAP_DLL)
    #define PCAP_API_DEF __declspec(dllimport)
  #else
    #define PCAP_API_DEF
  #endif
#else
  #ifdef pcap_EXPORTS
    #if PCAP_IS_AT_LEAST_GNUC_VERSION(3,4) || PCAP_IS_AT_LEAST_XL_C_VERSION(12,0)
      #define PCAP_API_DEF __attribute__((visibility("default")))
    #elif PCAP_IS_AT_LEAST_SUNC_VERSION(5,5)
      #define PCAP_API_DEF __global
    #else
      #define PCAP_API_DEF
    #endif
  #else
    #define PCAP_API_DEF
  #endif
#endif

#define PCAP_API PCAP_API_DEF

/* A selection of attribute macros (PCAP_NORETURN, PCAP_NONNULL, etc) are defined in upstream; we'll provide minimal compatibility here. */

#if __has_attribute(noreturn) || PCAP_IS_AT_LEAST_GNUC_VERSION(2,5) || PCAP_IS_AT_LEAST_XL_C_VERSION(7,0) || PCAP_IS_AT_LEAST_HP_C_CXX_VERSION(6,10) || __TINYC__
  #define PCAP_NORETURN __attribute((noreturn))
  #define PCAP_NORETURN_DEF __attribute((noreturn))
#elif defined(_MSC_VER)
  #define PCAP_NORETURN __declspec(noreturn)
  #define PCAP_NORETURN_DEF
#else
  #define PCAP_NORETURN
  #define PCAP_NORETURN_DEF
#endif

#endif /* lib_pcap_funcattrs_h */
