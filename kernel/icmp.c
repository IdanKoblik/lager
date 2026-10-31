#include "icmp.h"
#include "linux/icmp.h"
#include "linux/ip.h"
#include "linux/kern_levels.h"
#include "filter.h"
#include "linux/math.h"
#include <asm-generic/int-ll64.h>
#include <linux/in.h>
#include <linux/icmp.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>

#define PRINTABLE_ASCII_START 0x20
#define PRINTABLE_ASCII_END 0x7E
#define TOTAL_PRINTABLE_ASCII (PRINTABLE_ASCII_END - PRINTABLE_ASCII_START + 1)

#define PRINTABLE_FRACTION_NUM 95ULL
#define PRINTABLE_FRACTION_DEN 256ULL
#define PRINTABLE_FRACTION_COMPLEMENT (PRINTABLE_FRACTION_DEN - PRINTABLE_FRACTION_NUM)
#define MIN_ICMP_BYTES_BEFORE_DROP 100ULL
#define ICMP_ZSCORE_DROP_THRESHOLD 4

static u32 total_printable(unsigned char *target, size_t len) {
    u32 count = 0;
    if (len == 0)
        return 0;

    for (size_t i = 0; i < len; i++) {
        if (target[i] >= PRINTABLE_ASCII_START && target[i] <= PRINTABLE_ASCII_END)
            count++;
    }

    return count;
}

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

    u64 total_bytes = source->icmp.total_bytes;
    u64 printable_bytes = source->icmp.total_printable;

    // Keep the variance product and scaled numerator within 64-bit range
    while (total_bytes > (~0ULL / (PRINTABLE_FRACTION_NUM * PRINTABLE_FRACTION_COMPLEMENT))) {
        total_bytes >>= 1;
        printable_bytes >>= 1;
    }

    s64 numerator = (s64)(printable_bytes * PRINTABLE_FRACTION_DEN) - (s64)(total_bytes * PRINTABLE_FRACTION_NUM);
    u64 variance_scaled = total_bytes * PRINTABLE_FRACTION_NUM * PRINTABLE_FRACTION_COMPLEMENT;
    u32 denominator = int_sqrt64(variance_scaled);
    s64 z_score = denominator ? numerator / denominator : 0;
    bool newly_blocked = total_bytes >= MIN_ICMP_BYTES_BEFORE_DROP &&
                         z_score >= ICMP_ZSCORE_DROP_THRESHOLD;
    if (newly_blocked)
        source->icmp.blocked = true;
    *drop = source->icmp.blocked;
    spin_unlock_bh(&state->lock);

    if (newly_blocked)
        printk(KERN_WARNING "Dropping ICMP echo requests from %pI4 (bytes=%llu z=%lld)\n",
               &ip->saddr, (unsigned long long)total_bytes, (long long)z_score);

    return true;
}
