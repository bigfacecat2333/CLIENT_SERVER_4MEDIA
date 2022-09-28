#ifndef CLIENT_H__
#define CLIENT_H__

#define DEFAULT_PLAYER_CMD "user/bin/mpg123 -  > /dev/null"

struct client_conf_st
{
    char *rcv_port;
    char *mgroup_addr;
    char *player_cmd;
};

extern struct client_conf_st client_conf;

#endif