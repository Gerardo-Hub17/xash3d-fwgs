/*
 * NetGecko - Wii Network Debug Logger
 * Copyright (c) 2026 GatoWare
 *
 * Licensed under the Apache License, Version 2.0
 * https://github.com/oxyzin/netgecko
 */

#ifndef NETGECKO_H
#define NETGECKO_H

#include <string.h>
#include <ogcsys.h>
#include <gccore.h>
#include <network.h>

static int __ng_sock = -1;

static inline void NG_Init(const char *pc_ip, u16 port)
{
char localip[16], gateway[16], netmask[16];

if_config(localip, netmask, gateway, TRUE, 20);

__ng_sock = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);

struct sockaddr_in server;
memset(&server, 0, sizeof(server));
server.sin_family = AF_INET;
server.sin_port = htons(port);
server.sin_addr.s_addr = inet_addr(pc_ip);

net_connect(__ng_sock, (struct sockaddr *)&server, sizeof(server));
}

static inline void NG_Log(const char *msg)
{
if (__ng_sock >= 0)
et_send(__ng_sock, msg, strlen(msg), 0);
}

static inline void NG_Close(void)
{
if (__ng_sock >= 0) {
et_close(__ng_sock);
g_sock = -1;
}
}

#endif
