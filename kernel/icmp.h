#pragma once

#include "linux/icmp.h"
#include "linux/ip.h"
#include <linux/types.h>
#include "filter.h"

struct icmphdr *parse_icmp(struct iphdr *ip);
bool icmp_state_hook(struct filter *state, struct iphdr *ip, bool *drop);
