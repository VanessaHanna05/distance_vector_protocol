#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>      // close(), sleep()
#include <arpa/inet.h>   // inet_ntop(), htons(), ntohs()
#include <sys/socket.h>
#include <ifaddrs.h>     // getifaddrs()
#include <pthread.h>
#include <time.h>
#include <net/if.h>      // IFF_LOOPBACK

#define HELLO_INTERVAL 5   // send HELLO every 5 seconds
#define TIMEOUT 10         // neighbor timeout threshold (seconds)
#define PORT 5555          // UDP port for control messages
#define BUF_SIZE 1024

typedef struct {
    char ip[INET_ADDRSTRLEN];  // Neighbor IP (dotted decimal)
    uint16_t last_seq;         // Last seen sequence number
    time_t last_time;          // Time when last HELLO was received
} Neighbor;

// Global neighbor table (dynamic array)
Neighbor *neigh_table = NULL;
int neigh_count = 0;
int neigh_capacity = 0;
pthread_mutex_t neigh_mutex = PTHREAD_MUTEX_INITIALIZER;

// Local router info
char my_ip[INET_ADDRSTRLEN] = {0};
uint16_t my_seq = 0;   // Sequence number for HELLO messages
int sockfd;            // UDP socket descriptor

// Add a new neighbor or update existing one
void add_or_update_neighbor(const char *ip, uint16_t seq) {
    pthread_mutex_lock(&neigh_mutex);

    // Check if neighbor already exists
    for (int i = 0; i < neigh_count; i++) {
        if (strcmp(neigh_table[i].ip, ip) == 0) {
            // Existing neighbor: update if sequence is newer
            if (seq > neigh_table[i].last_seq) {
                neigh_table[i].last_seq = seq;
                neigh_table[i].last_time = time(NULL);
                printf("Updated neighbor %s (seq %u)\n", ip, seq);
            }
            pthread_mutex_unlock(&neigh_mutex);
            return;
        }
    }

    // New neighbor: add to table
    if (neigh_count == neigh_capacity) {
        neigh_capacity = (neigh_capacity == 0) ? 4 : neigh_capacity * 2;
        neigh_table = realloc(neigh_table, neigh_capacity * sizeof(Neighbor));
        if (!neigh_table) {
            perror("realloc");
            exit(1);
        }
    }

    strcpy(neigh_table[neigh_count].ip, ip);
    neigh_table[neigh_count].last_seq = seq;
    neigh_table[neigh_count].last_time = time(NULL);
    neigh_count++;
    printf("Discovered new neighbor %s (seq %u)\n", ip, seq);

    pthread_mutex_unlock(&neigh_mutex);
}

// Thread: send HELLO messages periodically
void* sender_thread(void *arg) {
    struct sockaddr_in bcast_addr;
    memset(&bcast_addr, 0, sizeof(bcast_addr));
    bcast_addr.sin_family = AF_INET;
    bcast_addr.sin_port = htons(PORT);
    bcast_addr.sin_addr.s_addr = inet_addr("255.255.255.255"); // broadcast

    while (1) {
        // Build HELLO message: "myIP:HELLO:" followed by 2-byte seq in network order
        char msg[BUF_SIZE];
        int len = snprintf(msg, sizeof(msg), "%s:HELLO:", my_ip);
        uint16_t netseq = htons(my_seq);
        memcpy(msg + len, &netseq, sizeof(netseq));
        len += sizeof(netseq);

        // Send via UDP broadcast
        if (sendto(sockfd, msg, len, 0, (struct sockaddr*)&bcast_addr, sizeof(bcast_addr)) < 0) {
            perror("sendto");
        } else {
            printf("Sent HELLO (seq=%u)\n", my_seq);
        }

        my_seq++; // increment sequence for next HELLO
        sleep(HELLO_INTERVAL);
    }
    return NULL;
}

// Thread: receive HELLO messages and update neighbor table
void* receiver_thread(void *arg) {
    while (1) {
        char buf[BUF_SIZE];
        struct sockaddr_in src;
        socklen_t srclen = sizeof(src);

        // Block until a message is received
        ssize_t n = recvfrom(sockfd, buf, sizeof(buf), 0,
                             (struct sockaddr*)&src, &srclen);
        if (n < 0) {
            perror("recvfrom");
            continue;
        }

        // Extract sender IP
        char src_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &src.sin_addr, src_ip, sizeof(src_ip));

        // Ignore our own broadcast
        if (strcmp(src_ip, my_ip) == 0) {
            continue;
        }

        // Parse message format "ip:HELLO:<2-byte seq>"
        if ((size_t)n >= BUF_SIZE) n = BUF_SIZE - 1;
        buf[n] = '\0'; // ensure null-terminated for parsing the text part

        char *colon = strchr(buf, ':');
        if (!colon) continue;

        *colon = '\0';
        char *sender_ip_str = buf; // text before first ':'
        (void)sender_ip_str;       // unused, we rely on src_ip instead

        char *token = colon + 1;
        if (strncmp(token, "HELLO:", 6) != 0) continue;

        char *seq_ptr = token + 6;
        if (n < (seq_ptr - buf + (ssize_t)sizeof(uint16_t))) continue; // incomplete

        // Read the 2-byte sequence number (network order -> host order)
        uint16_t netseq;
        memcpy(&netseq, seq_ptr, sizeof(netseq));
        uint16_t seq = ntohs(netseq);

        // Update neighbor table using the actual sender IP from packet
        add_or_update_neighbor(src_ip, seq);
    }
    return NULL;
}

// Thread: check for neighbor timeouts
void* timeout_thread(void *arg) {
    while (1) {
        sleep(1);
        time_t now = time(NULL);

        pthread_mutex_lock(&neigh_mutex);
        for (int i = 0; i < neigh_count; ) {
            if (difftime(now, neigh_table[i].last_time) > TIMEOUT) {
                // Remove stale neighbor (by swapping with last)
                printf("Neighbor %s timed out (no HELLO for %d sec)\n",
                       neigh_table[i].ip, TIMEOUT);
                neigh_table[i] = neigh_table[--neigh_count];
            } else {
                i++;
            }
        }
        pthread_mutex_unlock(&neigh_mutex);
    }
    return NULL;
}

int main() {
    // Determine local IPv4 address (skip loopback)
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        exit(1);
    }

    for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr &&
            ifa->ifa_addr->sa_family == AF_INET &&
            !(ifa->ifa_flags & IFF_LOOPBACK)) {

            struct sockaddr_in *sa = (struct sockaddr_in*)ifa->ifa_addr;
            inet_ntop(AF_INET, &sa->sin_addr, my_ip, sizeof(my_ip));
            break;
        }
    }
    freeifaddrs(ifaddr);

    if (strlen(my_ip) == 0) {
        fprintf(stderr, "Error: could not determine local IP\n");
        exit(1);
    }
    printf("Local IP: %s\n", my_ip);

    // Create UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(1);
    }

    // Allow broadcast on this socket
    int broadcastEnable = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST,
                   &broadcastEnable, sizeof(broadcastEnable)) < 0) {
        perror("setsockopt");
        exit(1);
    }

    // Bind to port 5555 on all interfaces
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(PORT);

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }

    // Launch threads: sender, receiver, and timeout checker
    pthread_t ts, tr, tt;
    if (pthread_create(&ts, NULL, sender_thread, NULL) != 0) {
        perror("pthread_create sender");
        exit(1);
    }
    if (pthread_create(&tr, NULL, receiver_thread, NULL) != 0) {
        perror("pthread_create receiver");
        exit(1);
    }
    if (pthread_create(&tt, NULL, timeout_thread, NULL) != 0) {
        perror("pthread_create timeout");
        exit(1);
    }

    // Wait for threads (program runs indefinitely)
    pthread_join(ts, NULL);
    pthread_join(tr, NULL);
    pthread_join(tt, NULL);

    close(sockfd);
    return 0;
}
