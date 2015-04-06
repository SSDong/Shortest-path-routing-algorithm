#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netdb.h>
#include <limits.h>

#define NBR_ROUTER 5

typedef struct {
    unsigned int router_id;
} pkt_INIT;

typedef struct {
    unsigned int router_id;
    unsigned int link_id;
} pkt_HELLO;

typedef struct {
    unsigned int sender;
    unsigned int router_id;
    unsigned int link_id;
    unsigned int cost;
    unsigned int via;
} pkt_LSPDU;

typedef struct {
    unsigned int link;
    unsigned int cost;
} link_cost;

typedef struct {
    unsigned int nbr_link;
    link_cost linkcost[NBR_ROUTER];
} circuit_DB;
