/*
 * mdns-repeater.c - Multicast DNS repeater
 * Copyright (C) 2017, kennylevinsen
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <errno.h>

#define MDNS_ADDR "224.0.0.251"
#define MDNS_PORT 5353
#define MAX_IFACES 16
#define BUFSIZE 9000

struct iface {
    char name[IFNAMSIZ];
    int fd;
    struct in_addr addr;
    struct ip_mreq mreq;
};
static struct iface ifaces[MAX_IFACES];
static int nifaces = 0;

static int add_iface(const char *ifname) {
    int fd;
    struct ifreq ifr;
    struct sockaddr_in sin;
    struct ip_mreq mreq;

    if (nifaces >= MAX_IFACES) {
        fprintf(stderr, "Too many interfaces\n");
        return -1;
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ-1);
    if (ioctl(fd, SIOCGIFADDR, &ifr) < 0) {
        perror("ioctl SIOCGIFADDR");
        close(fd);
        return -1;
    }

    memcpy(&ifaces[nifaces].addr,
           &((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr,
           sizeof(struct in_addr));
    strncpy(ifaces[nifaces].name, ifname, IFNAMSIZ-1);

    memset(&mreq, 0, sizeof(mreq));
    inet_pton(AF_INET, MDNS_ADDR, &mreq.imr_multiaddr);
    mreq.imr_interface = ifaces[nifaces].addr;
    if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        perror("setsockopt IP_ADD_MEMBERSHIP");
        close(fd);
        return -1;
    }

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    sin.sin_port = htons(MDNS_PORT);
    if (bind(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }

    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR");
    }

    ifaces[nifaces].fd = fd;
    ifaces[nifaces].mreq = mreq;
    nifaces++;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s iface1 iface2 [iface3 ...]\n", argv[0]);
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        if (add_iface(argv[i]) < 0) {
            fprintf(stderr, "Failed to add interface %s\n", argv[i]);
            return 1;
        }
    }

    char buf[BUFSIZE];
    fd_set rfds;
    while (1) {
        FD_ZERO(&rfds);
        int maxfd = 0;
        for (int i = 0; i < nifaces; i++) {
            FD_SET(ifaces[i].fd, &rfds);
            if (ifaces[i].fd > maxfd) maxfd = ifaces[i].fd;
        }

        if (select(maxfd + 1, &rfds, NULL, NULL, NULL) < 0) {
            perror("select");
            continue;
        }

        for (int i = 0; i < nifaces; i++) {
            if (!FD_ISSET(ifaces[i].fd, &rfds)) continue;

            struct sockaddr_in src;
            socklen_t srclen = sizeof(src);
            ssize_t r = recvfrom(ifaces[i].fd, buf, sizeof(buf), 0,
                                 (struct sockaddr *)&src, &srclen);
            if (r <= 0) continue;

            for (int j = 0; j < nifaces; j++) {
                if (i == j) continue;
                sendto(ifaces[j].fd, buf, r, 0,
                       (struct sockaddr *)&src, srclen);
            }
        }
    }
    return 0;
}
