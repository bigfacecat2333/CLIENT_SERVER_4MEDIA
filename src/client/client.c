#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <errno.h>
#include <error.h>
#include <string.h>

#include "proto.h"
#include "client.h"

/* struct: C端作为接受端，C端的要求没有必要告诉S端
int main()
{
    geopt();

    socket(); setsockopt(); bind();

    pipe();

    fork();

    //子进程调用解码器
    //父进程从网络收包并发送给子进程

    exit(0);
}
*/

struct client_conf_st client_conf = {
    .rcv_port = DEFAULT_RCVPORT,
    .mgroup_addr = DEFAULT_MGROUP,
    .player_cmd = DEFAULT_PLAYER_CMD};

static size_t writen(int fd, const void *buf, size_t len)
{
    int pos = 0;
    int ret = 0;
    while (len > 0)
    {
        ret = write(fd, buf, len);

        if (ret < 0)
        {
            if (errno == EINTR)
                continue;
            perror("write()");
            return -1;
        }

        len -= ret;
        pos += ret;
    }
    return 0;
}

static void printhelp(void)
{
    printf("-M --Mgroup 指定多播组\n");
    printf("-P --port   指定接受端口\n");
    printf("-H --help   显示帮助\n");
    printf("-p --player 指定播放器\n");
}

int main(int argc, char *argv[])
{
    int pd[2];
    int val = 1;
    int c;
    int sd;
    struct sockaddr_in laddr;
    struct ip_mreqn mreq;
    struct sockaddr_in server_addr, raddr;
    socklen_t server_addr_len, r_addr_len;
    int chosenid;
    int ret = 0;
    int index = 0;
    int len;
    struct option argarr[] = {{"port", 1, NULL, 'P'},
                              {"mgroup", 1, NULL, 'M'},
                              {"player", 1, NULL, 'p'},
                              {"help", 1, NULL, 'H'},
                              {NULL, 0, NULL, 0}};
    while (1)
    {
        c = getopt_long(argc, argv, "P:M:p:H", argarr, &index);
        if (c < 0)
        {
            break;
        }
        switch (c)
        {
        case 'P':
            client_conf.rcv_port = optarg;
            break;
        case 'M':
            client_conf.mgroup_addr = optarg;
            break;
        case 'p':
            client_conf.player_cmd = optarg;
            break;
        case 'H':
            printhelp();
            exit(0);
        default:
            abort();
            break;
        }
    }

    sd = socket(AF_INET, SOCK_DGRAM, 0); // 无连接报文, 0:IPPROTO_UDP

    if (sd < 0)
    {
        perror("socket()");
        exit(1);
    }

    // 加入多播组 man 7 ip
    inet_pton(AF_INET, client_conf.mgroup_addr, &mreq.imr_multiaddr); // 组播地址
    inet_pton(AF_INET, "0.0.0.0", &mreq.imr_address); // 本地地址
    mreq.imr_ifindex = if_nametoindex("ens33");       //网卡
    if (setsockopt(sd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
    {
        perror("setsockopt()");
        exit(1);
    }

    val = 1;
    //默认情况下，当本机发送组播数据到某个网络接口时，在IP层，数据会回送到本地的回环接口，
    //选项IP_MULTICAST_LOOP用于控制数据是否回送到本地的回环接口 1 为允许
    if (setsockopt(sd, IPPROTO_IP, IP_MULTICAST_LOOP, &val, sizeof(val)) < 0)
    {
        perror("setsockopt()");
        exit(1);
    }

    // 命名socket
    laddr.sin_family = AF_INET;
    laddr.sin_port = htons(atoi(client_conf.rcv_port));
    inet_pton(AF_INET, "0.0.0.0", &laddr.sin_addr.s_addr);
    if (bind(sd, (void *)&laddr, sizeof(laddr)) < 0)
    {
        perror("bind()");
        exit(1);
    }

    if (pipe(pd) < 0)
    {
        perror("pipe()");
        exit(1);
    }

    int pid = fork();
    if (pid < 0)
    {
        perror("pipe()");
        exit(1);
    }

    //子进程调用解码器
    if (pid == 0)
    {
        close(sd);
        close(pd[1]);
        dup2(pd[0], 0);
        if (pd[0] > 0)
            close(pd[0]);
        // sh -c Execute a command and then exit
        execl("/bin/sh", "sh", "-c", client_conf.player_cmd, NULL);
        perror("execl()");
        exit(1);
    }

    //父进程从网络收包并发送给子进程
    //收节目单，选择频道，收频道包(pipe)
    if (pid > 0)
    {
        //收节目单
        struct msg_list_st *msg_list;
        msg_list = malloc(MSG_LIST_MAX);
        if (msg_list == NULL)
        {
            perror("malloc()");
            exit(1);
        }
        while (1)
        {
            len = recvfrom(sd, msg_list, MSG_LIST_MAX, 0, (void *)&server_addr, &server_addr_len);
            if (len < sizeof(struct msg_list_st))
            {
                fprintf(stderr, "message is too small.\n");
                continue;
            }
            if (msg_list->chnid != LISTCHNID)
            {
                fprintf(stderr, "chnid is not list id\n");
                continue;
            }
            break;
        }
        // 打印节目单
        struct msg_listentry_st *pos;
        for (pos = msg_list->entry; (char *)pos < ((char *)msg_list + len); pos = (void *)((char *)pos) + ntohs(pos->len))
        {
            printf("channel %d:%s\n", pos->chnid, pos->desc);
        }

        free(msg_list);

        while (ret < 1)
        {
            ret = scanf("%d", &chosenid);
            if (ret != 1)
            {
                exit(1);
            }
        }

        // 收包并发送给子进程
        struct msg_channel_st *msg_channel;

        msg_channel = malloc(MSG_CHANNEL_MAX);
        if (msg_channel == NULL)
        {
            perror("malloc()");
            exit(1);
        }

        while (1)
        {
            len = recvfrom(sd, msg_channel, MSG_CHANNEL_MAX, 0, (void *)&raddr, &r_addr_len);
            if (raddr.sin_addr.s_addr != server_addr.sin_addr.s_addr) // 地址都不对
            {
                fprintf(stderr, "ignore:address not match\n");
                continue;
            }

            if (len < sizeof(struct msg_channel_st))
            {
                fprintf(stderr, "message too small\n");
                continue;
            }

            if (msg_channel->chnid == chosenid)
            {
                // 写管道, 坚持写够n个字节
                if (writen(pd[1], msg_channel->data, len - sizeof(chnid_t)) < 0)
                {
                    exit(1);
                }
            }
        }
        free(msg_channel);
        close(sd);

        exit(0);
    }

    exit(0);
}