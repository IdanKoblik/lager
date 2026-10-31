#include "filter.h"
#include "linux/netfilter.h"
#include <linux/kern_levels.h>
#include <linux/netfilter.h>
#include <linux/printk.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/net_namespace.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/icmp.h>
#include <net/ip.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Idan Koblik");
MODULE_DESCRIPTION("Kernel-level Network Steganalysis and Prevention");

#define IPV4_VERSION 4
#define IPV4_HEADER_LEN 5 

static struct nf_hook_ops nfho;
static struct filter filter_state;

static unsigned int hook(void *priv, struct sk_buff *skb, const struct nf_hook_state *nf_state) {
    if (!skb)
        return NF_ACCEPT;

    if (!pskb_may_pull(skb, sizeof(struct iphdr)))
        return NF_ACCEPT;

    struct iphdr *ip_header = ip_hdr(skb);
    if (ip_header->version != IPV4_VERSION || ip_header->ihl < IPV4_HEADER_LEN)
        return NF_ACCEPT;

    unsigned int header_len = ip_header->ihl * 4;
    if (!pskb_may_pull(skb, header_len))
        return NF_ACCEPT;

    ip_header = ip_hdr(skb);
    if (ip_is_fragment(ip_header))
        return NF_ACCEPT; // TODO handle

    if (ip_header->protocol == IPPROTO_ICMP &&
        !pskb_may_pull(skb, header_len + sizeof(struct icmphdr)))
        return NF_ACCEPT;

    ip_header = ip_hdr(skb);

    // TODO Parse ICMP and TCP SYN packets
    const char *proto_name = ip_proto_name(ip_header->protocol); 
    if (!proto_name)
        return NF_ACCEPT;

    printk(KERN_NOTICE "Inspecting %s packet\n", proto_name); 
    bool drop = false;
    if (!filter_hook(&filter_state, ip_header, &drop)) {
        printk(KERN_ERR "Lager Netfilter hook failed\n");
        return NF_ACCEPT;
    }

    if (drop) {
        printk(KERN_INFO "Dropping %s packet (ipv4=%pI4)\n", proto_name, &ip_header->saddr);
        return NF_DROP;
    }
    
    return NF_ACCEPT;
}

static int __init lager_init(void) {
    printk(KERN_INFO "Starting lager module.\n");
    int ret = 0;
    filter_init(&filter_state);

    printk(KERN_INFO "Initializing netfilter hook.\n");
    nfho.hook = hook;
    nfho.pf = PF_INET;
    nfho.hooknum = NF_INET_PRE_ROUTING;
    nfho.priority = NF_IP_PRI_FIRST;

    if ((ret = nf_register_net_hook(&init_net, &nfho)) < 0) {
        printk(KERN_ERR "Failed to register lager netfilter hook.\n");
        goto fail;
    }

    printk(KERN_INFO "Loaded lager module successfully.\n");
    return 0;
fail:
    printk(KERN_ERR "Failed to load lager module (err = %d).\n", ret);
    return ret;
}

static void __exit lager_exit(void) {
    printk(KERN_INFO "Exiting lager module.\n");
    
    nf_unregister_net_hook(&init_net, &nfho);
    printk(KERN_INFO "Lager module unregistered.\n");
}

module_init(lager_init);
module_exit(lager_exit);
