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

#define PRINTABLE_ASCII_START 0x20
#define PRINTABLE_ASCII_END 0x7E
#define TOTAL_PRINTABLE_ASCII (PRINTABLE_ASCII_END - PRINTABLE_ASCII_START + 1)

#define PRINTABLE_FRACTION (95 / 256)

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

bool icmp_state_hook(struct filter *state, struct iphdr *ip) {
    if (!state || !ip)
        return false;

    struct icmphdr *icmp = parse_icmp(ip);
    if (icmp == NULL) {
        printk(KERN_ERR "Failed to parse icmp packet\n");
        return false;
    }

    printk(KERN_DEBUG "Searching for entry (ipv4=%d)\n", ip->saddr);
    struct source_enrty *source = filter_get_or_create_source(state, ip->saddr);
    if (!source) {
        printk(KERN_ERR "Failed to load source (ipv4=%d)\n", ip->saddr);
        return false;
    }
    printk(KERN_DEBUG "Entry (ipv4=%d) was found\n", ip->saddr);

    // TODO method
    __be16 identifier = icmp->un.echo.id;
    source->icmp.total_bytes += sizeof(identifier);
    source->icmp.total_printable += total_printable((unsigned char *)&identifier, sizeof(identifier)); 

    u64 total_bytes = source->icmp.total_bytes;
    u64 total_printable = source->icmp.total_printable;
    u32 z_score = (total_printable - total_bytes * PRINTABLE_FRACTION) / int_sqrt((total_bytes * PRINTABLE_FRACTION) * (1 - PRINTABLE_FRACTION)); 
    printk(KERN_INFO "TEST: %d\n", z_score);

    return true;
}
