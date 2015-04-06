#include "router.h"

// Global variables, though not good but can be a bit more efficient
int router, router_id, nse_port, router_port;
char* nse_host;
char logFileName[30];
FILE* logFile;
struct sockaddr_in addrOfNse;
struct hostent* nseEntry;

void initialize() {
    struct addrinfo addr;
    int retval;
    
    //Initialize file
    sprintf(logFileName, "router%d.log", router_id);
    logFile = fopen(logFileName, "w+");

    // Reset structure
    memset(&addr, 0, sizeof(addr));   
    // Specify the desired address family for the returned addresses
    addr.ai_family = AF_INET;
    // Specify preferred socket type
    addr.ai_socktype = SOCK_DGRAM;  
    // Create a socket
    router = socket(addr.ai_family, addr.ai_socktype, 0);
    // Check return value
    if(router == -1) {
        fprintf(stderr, "Cannot initialize socket\n");
        exit(-1);
    }

    //Create rounter address
    struct sockaddr_in rAddr;
    rAddr.sin_addr.s_addr = INADDR_ANY;
    rAddr.sin_port = htons(router_port);
    rAddr.sin_family = AF_INET;

    // Create nse address
    addrOfNse.sin_family = AF_INET;
    addrOfNse.sin_port = htons(nse_port);
    char *first_host_addr = nseEntry->h_addr_list[0];
    int length = nseEntry->h_length;
    memmove(first_host_addr, &addrOfNse.sin_addr.s_addr, length);
    
    // Bind!
    retval = bind(router, (struct sockaddr*)&rAddr, sizeof(rAddr));
    if(retval == -1) {
       fprintf(stderr, "Cannot bind socket\n");
       exit(-1);
    }
}

void send_init_pkt() {
    int retval;
    // Construct an initialization pkt
    pkt_INIT initPkt;
    // Set the router id to itself
    initPkt.router_id = router_id;

    // Send it !
    retval = sendto(router, &initPkt, sizeof(initPkt), 0, (struct sockaddr*)&addrOfNse, sizeof(addrOfNse));
    if(retval == -1){
        fprintf(stderr, "Cannot send initialization packets\n");
        exit(-1);
    }
    // Print to log file
    fprintf(logFile, "R%d send a pkt_INIT\n", router_id);
}

void receive_circuit_database(circuit_DB *router_table, int *buf) {    
    int retval;
    struct sockaddr_in from;
    unsigned int fromlen = 0;

    // Receive !
    retval = recvfrom(router, buf, 100, 0, (struct sockaddr *)&from, &fromlen);
    if(retval == -1){
        fprintf(stderr, "Cannot receive circuit database\n");
        exit(-1);
    }
    // Print to log file
    fprintf(logFile, "R%d receive circuit database", router_id);

    //Initialise routing tables
    router_table[0].nbr_link = router_table[1].nbr_link = router_table[2].nbr_link = router_table[3].nbr_link = router_table[4].nbr_link = 0;

    //Receive from nse
    circuit_DB* fromNSE = (circuit_DB*)buf;
    memcpy(&router_table[router_id - 1], fromNSE, sizeof(circuit_DB));

    // Print to log file
    int i, j, links;
    fprintf(logFile, "# Topology database\n");
     
    for(i = 0; i < NBR_ROUTER; i++) {
        if(router_table[i].nbr_link) {
           fprintf(logFile, "R%d -> R%d nbr link %d\n", router_id, i + 1, router_table[i].nbr_link);
           links = router_table[i].nbr_link;
           for(j = 0; j < links; j++) {
               link_cost lc = router_table[i].linkcost[j];
               fprintf(logFile, "R%d -> R%d link %d cost %d\n", router_id, i + 1, lc.link, lc.cost);
           }
        } 
    }
}

void send_hello(circuit_DB *router_table) {
    int i, nbr_link, retval;
    nbr_link = router_table[router_id - 1].nbr_link;
    link_cost *linkcost = router_table[router_id - 1].linkcost;
    for(i = 0; i < nbr_link; i++){
        pkt_HELLO helloPkt;
        helloPkt.router_id = router_id;
        helloPkt.link_id = linkcost[i].link;
        retval = sendto(router, &helloPkt, sizeof(helloPkt), 0, (struct sockaddr*)&addrOfNse, sizeof(addrOfNse));
        if(retval == -1) {
           fprintf(stderr, "Cannot send hello packets\n");
           exit(-1);
        }
        fprintf(logFile, "R%d sent a hello packet via link %d\n", router_id, helloPkt.link_id);
    }
}

int getPrevious(int* topology, int current) {
  int via = current;
  for(; ;) {
     if(current == router_id - 1) break;
     via = current;
     current = topology[current];
  }
  return via + 1;
}

void printToLog(circuit_DB* router_table, int* topology, int* dist) {
     int i, j, links;
     // Print topology first
     fprintf(logFile, "# Topology database\n");
     
     for(i = 0; i < NBR_ROUTER; i++) {
         if(router_table[i].nbr_link) {
            fprintf(logFile, "R%d -> R%d nbr link %d\n", router_id, i + 1, router_table[i].nbr_link);
            links = router_table[i].nbr_link;
            for(j = 0; j < links; j++) {
                link_cost lc = router_table[i].linkcost[j];
                fprintf(logFile, "R%d -> R%d link %d cost %d\n", router_id, i + 1, lc.link, lc.cost);
            }
         } 
     }

     // Print RIB second
     fprintf(logFile, "# RIB\n");
     for(i = 0; i < NBR_ROUTER; i++) {
        if(i == router_id - 1) {
            fprintf(logFile, "R%d -> R%d -> local, 0\n", router_id, router_id);
        }
        else if(dist[i] == INT_MAX) {
            fprintf(logFile, "R%d -> R%d -> NA, INF\n", router_id, i + 1);
        }
        else {
            int via = getPrevious(topology, i);
            fprintf(logFile, "R%d -> R%d -> R%d, %d\n", router_id, i + 1, via, dist[i]);
        }
     }
}

void updateLinkStates(circuit_DB* router_table, pkt_LSPDU* receivedPkt) {
    int index = receivedPkt->router_id - 1;
    int newLink = router_table[index].nbr_link;
    ++router_table[index].nbr_link;
    router_table[index].linkcost[newLink].cost = receivedPkt->cost;
    router_table[index].linkcost[newLink].link = receivedPkt->link_id;
}

void checkChange(circuit_DB* router_table, pkt_LSPDU* receivedPkt, int* flag) {
    int links, i;
    link_cost* lc;
    links = router_table[receivedPkt->router_id - 1].nbr_link;
    *flag = 0;
    for(i = 0; i < links; i++) {
       lc = &(router_table[receivedPkt->router_id - 1].linkcost[i]);
       if(receivedPkt->link_id != lc->link) continue;
       else {
          // Yes! We found a same one, now check cost
          // Same cost, no need to update
          if(receivedPkt->cost == lc->cost) {
             return;
          }
          else { // Different cost
             // Update!
             lc->cost = receivedPkt->cost;
             *flag = 1;
             return;
          }
       }
    }
    // Didn't find it in our database, add a new one
    updateLinkStates(router_table, receivedPkt);
    *flag = 1;
}


// Helper function of dijkstra
int minDist(int* dist, int* included) {
    int minimum = INT_MAX, index, i;
    for(i = 0; i < NBR_ROUTER; i++) {
         if(dist[i] <= minimum && included[i] == 0) {
            index = i; 
            minimum = dist[i];
         }
    }
    return index;
}

// Helper function of dijkstra
int findOtherEnd(circuit_DB* router_table, link_cost lc, int current) {
    int i, j, links, ret = -100;
    for(i = 0; i < NBR_ROUTER; i++) {
        if(i != current) {
            links = router_table[i].nbr_link;
            for(j = 0; j < links; j++) {
                if(lc.link == router_table[i].linkcost[j].link) {
                    ret = i;
                    return ret;
                }
            }
        }
    }
    return ret;
}

void dijkstra(circuit_DB* router_table, int src) {
    int dist[NBR_ROUTER];
    int topology[NBR_ROUTER];
    int included[NBR_ROUTER];
    int i, counter, index, links, dest;
    // Initialize all distance as INFINITE and included[] as false
    for(i = 0; i < NBR_ROUTER; i++) {
        dist[i] = INT_MAX;
        included[i] = 0;
        topology[i] = -1;
    }
    // Distance of source to itself is 0
    dist[src] = 0;
    // Find shortest path for all nodes
    for(counter = 0; counter < NBR_ROUTER; counter++) {
        // Find the minimum distance node which is not included
        index = minDist(dist, included);
        // Now node at index is included
        included[index] = 1;
        // Now we need to update the edges which are attached to this node
        links = router_table[index].nbr_link;
        // Loop through!
        for(i = 0; i < links; i++) {
            link_cost lc = router_table[index].linkcost[i];
            dest = findOtherEnd(router_table, lc, index);
            if(dest != -100 && (lc.cost + dist[index] < dist[dest])) {
                topology[dest] = index;
                dist[dest] = lc.cost + dist[index];
            }
        }
    }
    // Print both topology and RIB to the log file
    printToLog(router_table, topology, dist);
}

void send_and_receive_PDU(circuit_DB* router_table, int* buf) {
    struct sockaddr_in from;
    unsigned int fromlen = 0;
    int retval, count = 0, i, flag;
    fd_set waitForInput;
    FD_ZERO(&waitForInput);
    FD_SET(router, &waitForInput);
    int incomingLinks[5];

    // Set timeout of 3 seconds 
    struct timeval timeout;
    timeout.tv_usec = 0;
    timeout.tv_sec = 3;

    // Infinite loop
    for( ; ; ){
        retval = select(1 + router, &waitForInput, NULL, NULL, &timeout);
        if (retval == 0) {
            break;
        }
        else {
            memset(buf, 0, 100 * sizeof(int));
            retval = recvfrom(router, buf, 100, 0, (struct sockaddr*)&from, &fromlen);
            if(retval == -1) {
                fprintf(stderr, "Cannot receive LSPDU\n");
                exit(-1);
            }
            
            // If its size is of 5 ints large, then it's pkt_LSPDU packet
            if(retval == 5 * sizeof(int)) {
                pkt_LSPDU* receivedPkt = (pkt_LSPDU*) buf;
                // write to the log 
                fprintf(logFile, "R%d received pkt_LSPDU from router%d via link%d with cost%d\n",router_id, receivedPkt->router_id, receivedPkt->link_id, receivedPkt->cost);
                //If our link state db got updated
                checkChange(router_table, receivedPkt, &flag);
                if(flag) {
                    // Runs a Shortest Path First algorithm based on Dijkstra algorithm
                    dijkstra(router_table, router_id - 1);
                    // Inform each of the rest neighbours by forwarding this received LS_PDU
                    receivedPkt->sender = router_id;
                    for(i = 0; i < count; i++) {
                        if(receivedPkt->via == incomingLinks[i]) continue;
                        else {
                            receivedPkt->via = incomingLinks[i];
                            retval = sendto(router, receivedPkt, sizeof(*receivedPkt), 0, (struct sockaddr*)&addrOfNse, sizeof(addrOfNse));
                            if(retval == -1){
                                fprintf(stderr, "Cannot forward LSPDU\n");
                                exit(-1);
                            }
                            fprintf(logFile, "R%d sent pkt_LSPDU to link%d with link%d and cost%d\n", router_id, receivedPkt->via, receivedPkt->link_id, receivedPkt->cost);
                        }
                    }
                }
            } else {
                // else (i.e it's size is of 2 ints large, it's pkt_HELLO packet 
                pkt_HELLO *helloPkt = (pkt_HELLO*) buf;
                incomingLinks[count] = helloPkt->link_id;
                ++count;
                fprintf(logFile, "R%d received pkt_HELLO via link%d\n", router_id, helloPkt->link_id);
                // Send a set of LS_PDU pkts(struct pkt_LSPDU) to that neighbour. Each LS_PDU
                // corresponds to a single link within the database
                int links = router_table[router_id - 1].nbr_link;
                for(i = 0; i < links; i++){
                    pkt_LSPDU pkt;
                    pkt.sender = router_id;
                    pkt.router_id = router_id;
                    pkt.link_id = router_table[router_id - 1].linkcost[i].link;
                    pkt.cost  = router_table[router_id - 1].linkcost[i].cost;
                    pkt.via = helloPkt->link_id;
                    retval = sendto(router, &pkt, sizeof(pkt), 0, (struct sockaddr*)&addrOfNse, sizeof(addrOfNse));
                    if(retval == -1){
                        fprintf(stderr, "Cannot send LSPDU\n");
                        exit(-1);
                    }
                    fprintf(logFile, "R%d sent pkt_LSPDU to link%d with link%d and cost%d\n", router_id, pkt.via, pkt.link_id, pkt.cost);
                }
            }
        }
    }
}


int main (int argc, char *argv[]) {
    // Check arguments
    if(argc != 5) {
        printf("Plese enter the following arguments <router_id> <nse_host> <nse_port> <router_port>\n");
        return 0;
    }   

    // Assign values from inputs
    router_id = atoi(argv[1]);
    router_port = atoi(argv[4]);
    nse_port = atoi(argv[3]);
    nse_host = argv[2];
    nseEntry = gethostbyname(nse_host);

    // Define a buffer
    int buf[100];
    // Our database
    circuit_DB router_table[NBR_ROUTER];

    // Initialize settings
    initialize();
    // Send initialization packets
    send_init_pkt();
    // Receive the circuit database
    receive_circuit_database(router_table, buf);
    // Send hello packets
    send_hello(router_table);
    // Start
    send_and_receive_PDU(router_table, buf);

    return 0;
}
