#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>      
#include <arpa/inet.h>   
#include <sys/socket.h>
#include <ifaddrs.h>     
#include <pthread.h>
#include <time.h>
#include <net/if.h>      

/*What each library is used for:
<stdio.h> for printf, perror, fprintf
<stdlib.h> for  malloc, realloc, exit, free
<string.h> for strcmp, strcpy, memcpy, strlen, strncmp, strchr, memset
<unistd.h> for close, sleep
<arpa/inet.h> for inet_ntop, inet_addr, htons, ntohs, htonl
<sys/socket.h> for socket, bind, sendto, recvfrom, struct sockaddr, struct sockaddr_in
<ifaddrs.h> for getifaddrs, freeifaddrs
<pthread.h> for pthread_t, pthread_create, pthread_join, pthread_mutex_t, pthread_mutex_lock, pthread_mutex_unlock
<time.h> for time, difftime, time_t
<net/if.h> for IFF_LOOPBACK*/

#define HELLO_INTERVAL 5   // send HELLO every 5 seconds
#define TIMEOUT 10         // neighbor timeout threshold 
#define PORT 5555          // UDP port 
#define BUF_SIZE 1024      //Used to size char msg[BUF_SIZE] and char buf[BUF_SIZE]

//Neighbor structure that contains the IP address, the last sequence of the hello from the specific neighbor and the time it sent the last hello so we can track if it is still there or not
typedef struct {
    char ip[INET_ADDRSTRLEN];  // char array to store IP as text and INET_ADDRSTRLEN is a constant from <arpa/inet.h> for max IPv4 string length
    uint16_t last_seq;         // Last seen sequence number seen from the neighbor
    time_t last_time;          // Time when last HELLO was received and Used by timeout_thread to see if the neighbor has timed out
} Neighbor;


Neighbor *neigh_table = NULL; // this is the pointer to an array that is dynamically allocated using reaclloc and contains the neighbors that are found 
int neigh_count = 0; // the size of the array == the number of neighbors that are currently found 
int neigh_capacity = 0; // the capacity of the current malloc in memory that we need to check before adding a neighbor to be able to change it based on the need 
pthread_mutex_t neigh_mutex = PTHREAD_MUTEX_INITIALIZER; // this is a mutex to protect the array of neighbors 
char my_ip[INET_ADDRSTRLEN] = {0}; //this is the router"s IP address in characters  
uint16_t my_seq = 0;   // Sequence number for HELLO messages or counter incremented in sender_thread after each HELLO
int sockfd;            // UDP socket used in the sender and receiver functions 


void add_or_update_neighbor(const char *ip, uint16_t seq) {
    // this function adds the neighbor if it is new (so its ip address is already in the array of neighbors, or it updates the sequence number of the neighbor)
    // it takes the ip address of the neighbor and its sequence number 
    pthread_mutex_lock(&neigh_mutex);
    // lock the mutex so only one thread manipulates the neighbor table at a time

    // In this loop we are checking if the neighbor already there by comparing its ip address with the stored ip addresses in the neigh_table 
    for (int i = 0; i < neigh_count; i++) {
        if (strcmp(neigh_table[i].ip, ip) == 0) {
            // if the neighbor exists inside the array we need to update the sequence number if it is incremented (check if current sequence is > the sequence for the found ip address)
            if (seq > neigh_table[i].last_seq) {
                neigh_table[i].last_seq = seq;
                neigh_table[i].last_time = time(NULL); //time(NULL) returns the current time so we need to update the time of the neighbor to check if there is a timeout 
                printf("Updated neighbor %s (seq %u)\n", ip, seq);
            }
            pthread_mutex_unlock(&neigh_mutex);// unlocking this mutex so the other threads can work
            return;
        }
    }

    // If the IP address of the current nieghbor is not found in the table we need to check for the capacity of the neigh_table and if needed we need to double the capacity 
    // Check if we have enough capacity: If capacity is 0, start with 4
    // Else double the capacity 
    // realloc changes the size of the allocated memory for neigh_table

    if (neigh_count == neigh_capacity) {

        if (neigh_capacity == 0) {
            neigh_capacity = 4;
        } else {
            neigh_capacity = neigh_capacity * 2;
        }   
        neigh_table = realloc(neigh_table, neigh_capacity * sizeof(Neighbor));
    }

    strcpy(neigh_table[neigh_count].ip, ip);
    //Fill in the new entry at index neigh_count
    neigh_table[neigh_count].last_seq = seq;
    neigh_table[neigh_count].last_time = time(NULL);
    neigh_count++; //Increment neigh_count
    printf("Discovered new neighbor %s (seq %u)\n", ip, seq);
    pthread_mutex_unlock(&neigh_mutex);
}

// This function creates a thread that runs forever, so constantly it will send every 5 seconds a Hello message as a broadcast and it stops if you stop the code 
void* sender_thread(void *arg) {
    (void)arg; // this is added because warning in the terminal were noticed  because we are not using the argument but it is needed for the thread function 
    struct sockaddr_in bcast_addr;
    //struct sockaddr_in is a structure for IPv4 addresses. it contains: P address , port, address family (IPv4), padding fields
    memset(&bcast_addr, 0, sizeof(bcast_addr)); //this function memset is used to set every byte inside the struct bcast_addr equal to zero
    bcast_addr.sin_family = AF_INET; // for IPv4
    bcast_addr.sin_port = htons(PORT); //htons converts a 16-bit value from the host's native byte order into network byte order which is required by all Internet protocols
    bcast_addr.sin_addr.s_addr = inet_addr("255.255.255.255"); // broadcast address

    while (1) {
        // Build HELLO message: "myIP:HELLO:" followed by 2-byte seq in network order
        char msg[BUF_SIZE];
        int len = snprintf(msg, sizeof(msg), "%s:HELLO:", my_ip);
        uint16_t netseq = htons(my_seq);
        memcpy(msg + len, &netseq, sizeof(netseq));
        len += sizeof(netseq);
        if (sendto(sockfd, msg, len, 0, (struct sockaddr*)&bcast_addr, sizeof(bcast_addr)) < 0) { //sendto() sends the HELLO as a UDP datagram to broadcast
            perror("sendto");
        } else {
            printf("Sent HELLO (seq=%u)\n", my_seq);
        }
        my_seq++; //Increment sequence number for next HELLO
        sleep(HELLO_INTERVAL);
    }
    return NULL;
}

// Thread: receive HELLO messages and update neighbor table
void* receiver_thread(void *arg) {
    (void)arg;// this is added because warning in the terminal were noticed  because we are not using the argument but it is needed for the thread function
    while (1) {
        char buf[BUF_SIZE]; // this buffer is used to save incoming data and use it to either add a neighbor or update its sequence (The UDP payload is copied into buf)
        // so the buffer containes text bytes (ASCII characters) PLUS 2 raw binary bytes (sequence number)


        struct sockaddr_in src; // will hold the source address of the sender (The sender’s IP and port go into src)
        // The src after the socket receives a packet will contain:
        // src.sin_family → AF_INET (2)
        // src.sin_port → sender’s UDP port (in network byte order)
        // src.sin_addr.s_addr → sender’s binary IP address (in binary)
        socklen_t srclen = sizeof(src);

        // We just wait until we receive a message 
        //The socket sockfd is bound to port 5555, so any UDP packet sent to this port will wake up the recvfrom() thread
        ssize_t n = recvfrom(sockfd, buf, sizeof(buf), 0, (struct sockaddr*)&src, &srclen);
        //n is the number of bytes received and if something wront the functino recvfrom will return -1

        if (n < 0) {
            perror("recvfrom");
            continue;
        }
        // Extract sender IP
        char src_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &src.sin_addr, src_ip, sizeof(src_ip)); //inet_ntop converts IP from binary to text, we convert to string to be able to compare 
         
        if (strcmp(src_ip, my_ip) == 0) {
            continue; // we need to make sure that we do not inlcude ourselfs in the table 
        }
        buf[n] = '\0'; // we add the null termination to be abel to parse the strings 

        char *colon = strchr(buf, ':'); //finds the first ':' in the message
        if (!colon) continue; // if not found we will ignore the packet 

        char *token = colon + 1;
        if (strncmp(token, "HELLO:", 6) != 0) continue; //token points out to the Hello message and if the Hello message is not there skip

        char *seq_ptr = token + 6;

        // Read the 2-byte sequence number 
        uint16_t netseq;
        memcpy(&netseq, seq_ptr, sizeof(netseq));
        uint16_t seq = ntohs(netseq);

        // Update neighbor table using the actual sender IP from packet
        add_or_update_neighbor(src_ip, seq);
    }
    return NULL;
}

void* timeout_thread(void *arg) {
    (void)arg; // this is added because warning in the terminal were noticed  because we are not using the argument but it is needed for the thread function
    while (1) {
        time_t now = time(NULL); //get the current time
        pthread_mutex_lock(&neigh_mutex); // lock the array of neighbors
        for (int i = 0; i < neigh_count; ) {
            if (difftime(now, neigh_table[i].last_time) > TIMEOUT) { //If now - last_time > TIMEOUT, neighbor is considered dead
                // Remove stale neighbor (by swapping with last)
                printf("Neighbor %s timed out (no HELLO for %d sec)\n", neigh_table[i].ip, TIMEOUT);
                neigh_table[i] = neigh_table[--neigh_count];
            } else {
                i++; // the i++ is set here becasue if we keep it in the for loop and we remove one neighbor the size of the neigh_table will be -1 and we will have index out of bounds
            }
        }
        pthread_mutex_unlock(&neigh_mutex);
    }
    return NULL;
}

int main() {
   
    struct ifaddrs *ifaddr, *ifa;
    //ifaddrs is a struct that describes a network interface (like veth-r1) and its addresses
    //ifaddr will point to the head of a linked list of all interfaces
    //ifa will be used to walk through (iterate over) that list
    if (getifaddrs(&ifaddr) == -1) {
        /*
        Fills ifaddr with a linked list of all network interfaces and their addresses in this process’s namespace
        In r1 namespace: you’ll see lo and veth-r1
        In r2: lo, veth-r2, etc
        Returns -1 on error*/

        perror("getifaddrs");
        exit(1);
    }
    // this loops over each interface available in ifaddrs
    for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr &&  ifa->ifa_addr->sa_family == AF_INET && !(ifa->ifa_flags & IFF_LOOPBACK)) {
            //ifa->ifa_addr makes sure this interface has an address sa_family == AF_INET makes sure that this IPv4 addresses !(ifa->ifa_flags & IFF_LOOPBACK) skips loopback (lo)
            struct sockaddr_in *sa = (struct sockaddr_in*)ifa->ifa_addr;
            inet_ntop(AF_INET, &sa->sin_addr, my_ip, sizeof(my_ip)); //sa->sin_addr is the binary IPv4 address and inet_ntop converts it to a human-readable string
            break;
        }
    }
    freeifaddrs(ifaddr);

    printf("Local IP: %s\n", my_ip);

    // Create UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    /*AF_INET means IPv4 
    SOCK_DGRAM is for UDP (datagram)
    0 for default protocol (UDP for AF_INET/SOCK_DGRAM)*/

    if (sockfd < 0) {
        perror("socket");
        exit(1);
    }

    // Allow broadcast on this socket
    int broadcastEnable = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable)) < 0) {
        //setsockopt with SO_BROADCAST turns on permission to send to 255.255.255.255 and put the pointer to 1 to enable broadcast it it was 0 it will disable it
        perror("setsockopt");
        exit(1);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr)); // used to clear the addr structure from garbage values in the memory
    addr.sin_family = AF_INET; // means IPV4
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // this is to listen to all local IPs on the network
    addr.sin_port = htons(PORT); // this is to listen to port 5555

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { // we are telling the OS that the soclet is listening on UDP Port 5555 on this machine
        perror("bind");
        exit(1);
    }

    // Launch threads: sender, receiver, and timeout checker
    pthread_t sender_th, receiver_th, timeout_th;
    if (pthread_create(&sender_th, NULL, sender_thread, NULL) != 0) {
        perror("pthread_create sender");
        exit(1);
    }
    if (pthread_create(&receiver_th, NULL, receiver_thread, NULL) != 0) {
        perror("pthread_create receiver");
        exit(1);
    }
    if (pthread_create(&timeout_th, NULL, timeout_thread, NULL) != 0) {
        perror("pthread_create timeout");
        exit(1);
    }

    // We have these to close the main program once all the threads finish their work but in our case the threads are while(1) loops that runn forever unless we manually stop them in the terminal 
    pthread_join(sender_th, NULL);
    pthread_join(receiver_th, NULL);
    pthread_join(timeout_th, NULL);

    close(sockfd);
    return 0;
}
