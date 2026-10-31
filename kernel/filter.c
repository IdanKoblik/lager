#include "filter.h"
#include "icmp.h"
#include "linux/gfp_types.h"
#include "linux/hashtable.h"
#include "linux/in.h"
#include "linux/ip.h"
#include "linux/mutex.h"
#include "linux/slab.h"
#include "linux/spinlock.h"
#include "linux/types.h"
#include <linux/module.h>
#include <linux/string.h>

const char *ip_proto_name(u8 protocol) {
    switch (protocol) {
        case IPPROTO_ICMP: return "ICMP";
        // case IPPROTO_TCP:  return "TCP";
        default:           return NULL;
    }
}

void filter_init(struct filter *state) {
    if (!state)
        return;

    memset(state, 0, sizeof(*state));
    // NOLINTNEXTLINE(bugprone-sizeof-expression): Linux hash macros size the table internally.
    hash_init(state->sources);
    spin_lock_init(&state->lock);
}

struct source_enrty *filter_get_or_create_source(struct filter *state, __be32 ip) {
    if (!state)
        return NULL;

    struct source_enrty *entry = {0};
    spin_lock_bh(&state->lock); 
   
    // NOLINTNEXTLINE(bugprone-sizeof-expression): Linux hash macros size the table internally.
    hash_for_each_possible(state->sources, entry, node, (__force u32)ip) {
        if (entry->source_ip == ip) {
            spin_unlock_bh(&state->lock);
            return entry;
        }
    }

    entry = kzalloc(sizeof(*entry), GFP_ATOMIC);
    if (!entry) {
        spin_unlock_bh(&state->lock);
        printk(KERN_ERR "Failed to allocate source entry (ipv4=%pI4)\n", &ip);
        return NULL;
    }
    
    entry->source_ip = ip;
    // NOLINTNEXTLINE(bugprone-sizeof-expression): Linux hash macros size the table internally.
    hash_add(
        state->sources,
        &entry->node,
        (__force u32)ip
    );

    spin_unlock_bh(&state->lock);
    return entry;
}

bool filter_hook(struct filter *state, struct iphdr *ip, bool *drop) {
    if (!state || !ip || !drop)
        return false;

    *drop = false;
    if (ip->protocol == IPPROTO_ICMP)
        return icmp_state_hook(state, ip, drop);
    
    return true;
}
