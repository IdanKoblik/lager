#pragma once

#include "asm-generic/int-ll64.h"
#include "linux/hashtable.h"
#include "linux/in.h"
#include "linux/ip.h"
#include "linux/spinlock_types.h"
#include "linux/types.h"

#define SOURCE_HASH_BITS 12

struct icmp_stats {
    u64 total_bytes;
    u64 total_printable;
};

struct source_enrty {
    struct hlist_node node;
    __be32 source_ip;

    struct icmp_stats icmp;
};

const char *ip_proto_name(u8 protocol); 

struct filter {
    DECLARE_HASHTABLE(sources, SOURCE_HASH_BITS);
    spinlock_t lock;
};

void filter_init(struct filter *state);
struct source_enrty *filter_get_or_create_source(struct filter *state, __be32 ip);
bool filter_hook(struct filter *state, struct iphdr *ip);
