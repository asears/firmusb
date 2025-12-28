/* Minimal vendored socket.h */
#ifndef lib_pcap_socket_h
#define lib_pcap_socket_h

#ifdef _WIN32
  #ifdef __MINGW32__
    #include <windef.h>
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef long suseconds_t;
  #ifndef PCAP_SOCKET
    #define PCAP_SOCKET SOCKET
  #endif
#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netdb.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #ifndef PCAP_SOCKET
    #define PCAP_SOCKET int
  #endif
  #ifndef INVALID_SOCKET
    #define INVALID_SOCKET -1
  #endif
#endif

#endif /* lib_pcap_socket_h */
