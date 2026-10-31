#include "icmp.h"
#include "linux/icmp.h"
#include "linux/ip.h"
#include "linux/kern_levels.h"
#include "filter.h"
#include "linux/math.h"
#include "stats/ascii.h"
#include <asm-generic/int-ll64.h>
#include <linux/in.h>
#include <linux/icmp.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>

#define MIN_ICMP_BYTES_BEFORE_DROP 100ULL
#define ICMP_ZSCORE_DROP_THRESHOLD 4

struct icmphdr *parse_icmp(struct iphdr *ip) {
    if (!ip)
        return NULL;

    if (ip->protocol != IPPROTO_ICMP)
        return NULL;

    struct icmphdr *icmp = (void *)ip + (ip->ihl * 4);
    if (!icmp) {
        printk(KERN_ERR "Cannot locate icmp data.\n");
        return NULL;
    }

    return icmp;
}

bool icmp_state_hook(struct filter *state, struct iphdr *ip, bool *drop) {
    if (!state || !ip || !drop)
        return false;

    *drop = false;

    struct icmphdr *icmp = parse_icmp(ip);
    if (icmp == NULL) {
        printk(KERN_ERR "Failed to parse icmp packet\n");
        return false;
    }

    if (icmp->type != ICMP_ECHO || icmp->code != 0)
        return true;

    printk(KERN_DEBUG "Searching for entry (ipv4=%pI4)\n", &ip->saddr);
    struct source_enrty *source = filter_get_or_create_source(state, ip->saddr);
    if (!source) {
        printk(KERN_ERR "Failed to load source (ipv4=%pI4)\n", &ip->saddr);
        return false;
    }
    printk(KERN_DEBUG "Entry (ipv4=%pI4) was found\n", &ip->saddr);

    __be16 identifier = icmp->un.echo.id;

    spin_lock_bh(&state->lock);
    if (source->icmp.blocked) {
        *drop = true;
        spin_unlock_bh(&state->lock);
        return true;
    }

    source->icmp.total_bytes += sizeof(identifier);
    source->icmp.total_printable += total_printable((unsigned char *)&identifier, sizeof(identifier)); 

    s64 ascii_stat = calculate_ascii_stat(
        (unsigned char *)&identifier,
        sizeof(identifier),
        &source->icmp.total_printable,
        &source->icmp.total_bytes
    );

    bool newly_blocked = source->icmp.total_bytes >= MIN_ICMP_BYTES_BEFORE_DROP && ascii_stat >= ICMP_ZSCORE_DROP_THRESHOLD;
    if (newly_blocked)
        source->icmp.blocked = true;
    *drop = source->icmp.blocked;
    spin_unlock_bh(&state->lock);

    if (newly_blocked)
        printk(KERN_WARNING "Dropping ICMP echo requests from %pI4 (bytes=%llu z=%lld)\n",
               &ip->saddr, (unsigned long long)source->icmp.total_bytes, (long long)ascii_stat);

    return true;
}
