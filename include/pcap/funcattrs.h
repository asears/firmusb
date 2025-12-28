/* Minimal vendored funcattrs.h for libpcap compatibility */
#ifndef lib_pcap_funcattrs_h
#define lib_pcap_funcattrs_h

#if defined(_WIN32)
  #if defined(pcap_EXPORTS)
    #define PCAP_API_DEF __declspec(dllexport)
  #elif defined(PCAP_DLL)
    #define PCAP_API_DEF __declspec(dllimport)
  #else
    #define PCAP_API_DEF
  #endif
#else
  #define PCAP_API_DEF
#endif

#define PCAP_API PCAP_API_DEF

#if defined(_MSC_VER)
  #define PCAP_NORETURN __declspec(noreturn)
#else
  #define PCAP_NORETURN
#endif

#endif /* lib_pcap_funcattrs_h */
