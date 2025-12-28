/* Minimal vendored bpf.h */
#ifndef lib_pcap_bpf_h
#define lib_pcap_bpf_h

#include <pcap/funcattrs.h>
#include <pcap/dlt.h>

typedef int bpf_int32;
typedef unsigned int bpf_u_int32;

struct bpf_program { unsigned int bf_len; struct bpf_insn *bf_insns; };
struct bpf_insn { unsigned short code; unsigned char jt; unsigned char jf; bpf_u_int32 k; };

#endif /* lib_pcap_bpf_h */
