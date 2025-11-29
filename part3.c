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


#ifndef IFF_LOOPBACK
#define IFF_LOOPBACK 0x0008
#endif

#define HELLO_INTERVAL 5
#define TIMEOUT 10
#define PORT 5555
#define BUF_SIZE 1024

// ======== PART 3 CRITICAL FIX FOR WSL2 ========
// We dynamically compute the interface broadcast (10.0.0.255)
// instead of using 255.255.255.255 (WSL2 blocks global broadcast)
 char broadcast_ip[INET_ADDRSTRLEN] = {0};

// ======== External Part 2 Functions ========
extern void set_my_ip(const char *ip); 
extern char* getDistanceVector(void);
extern void processDistanceVector(char *msg);
extern void dvSent(void);
extern int isDvUpdated(void);
extern void updateRoute(const char *dest, const char *via, int cost);
extern void removeAllRoutesVia(const char *via);

// ======== Neighbor Table ========
typedef struct {
    char ip[INET_ADDRSTRLEN];
    uint16_t last_seq;
    time_t last_time;
} Neighbor;

static Neighbor *neigh_table = NULL;
static int neigh_count = 0;
static int neigh_capacity = 0;

static pthread_mutex_t neigh_mutex = PTHREAD_MUTEX_INITIALIZER;

 char my_ip[INET_ADDRSTRLEN] = {0};
static int sockfd;
static uint16_t my_seq = 0;

// ===================================
// HELPERS
// ===================================

void compute_broadcast_from_ip(const char *ip)
{
    // assumes /24 networks like assignment says
    // e.g. 10.0.0.1 -> 10.0.0.255
    struct in_addr addr;
    inet_pton(AF_INET, ip, &addr);

    uint32_t ip_int = ntohl(addr.s_addr);
    uint32_t bcast = (ip_int & 0xFFFFFF00) | 0x000000FF;

    struct in_addr baddr;
    baddr.s_addr = htonl(bcast);
    inet_ntop(AF_INET, &baddr, broadcast_ip, sizeof(broadcast_ip));

    printf("Computed broadcast address = %s\n", broadcast_ip);
}

// ===================================
// NEIGHBOR TABLE
// ===================================

void add_or_update_neighbor(const char *ip, uint16_t seq)
{
    pthread_mutex_lock(&neigh_mutex);

    for (int i = 0; i < neigh_count; i++) {
        if (strcmp(neigh_table[i].ip, ip) == 0) {
            if (seq > neigh_table[i].last_seq) {
                neigh_table[i].last_seq = seq;
                neigh_table[i].last_time = time(NULL);
                printf("Updated neighbor %s (seq %u)\n", ip, seq);
            }
            pthread_mutex_unlock(&neigh_mutex);
            return;
        }
    }

    // new neighbor
    if (neigh_count == neigh_capacity) {
        neigh_capacity = (neigh_capacity == 0) ? 4 : neigh_capacity * 2;
        neigh_table = realloc(neigh_table, neigh_capacity * sizeof(Neighbor));
    }

    strncpy(neigh_table[neigh_count].ip, ip, sizeof(neigh_table[neigh_count].ip));
    neigh_table[neigh_count].last_seq = seq;
    neigh_table[neigh_count].last_time = time(NULL);
    neigh_count++;

    printf("Discovered new neighbor %s (seq %u)\n", ip, seq);

    pthread_mutex_unlock(&neigh_mutex);
}

// ===================================
// SENDER THREAD
// ===================================

void* sender_thread(void *arg)
{
    (void)arg;

    struct sockaddr_in baddr;
    memset(&baddr, 0, sizeof(baddr));
    baddr.sin_family = AF_INET;
    baddr.sin_port   = htons(PORT);

    // ======== WSL2 FIX: use interface broadcast (10.0.0.255) ========
    baddr.sin_addr.s_addr = inet_addr(broadcast_ip);

    while (1) {

        char msg[BUF_SIZE];
        int len = snprintf(msg, sizeof(msg), "%s:HELLO:", my_ip);

        uint16_t netseq = htons(my_seq);
        memcpy(msg + len, &netseq, sizeof(netseq));
        len += sizeof(netseq);

        if (sendto(sockfd, msg, len, 0, (struct sockaddr *)&baddr, sizeof(baddr)) < 0) {
            perror("sendto HELLO");
        } else {
            printf("Sent HELLO (seq=%u)\n", my_seq);
        }

        my_seq++;
        sleep(HELLO_INTERVAL);
    }
    return NULL;
}

// ===================================
// DV SENDER
// ===================================
static void choose_local_ip_and_broadcast(char *ip_out, size_t ip_len,
                                          char *bcast_out, size_t bcast_len)
{
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        exit(1);
    }

    struct sockaddr_in best_addr = {0};
    struct sockaddr_in best_mask = {0};
    int found = 0;

    // First pass: prefer 10.0.0.x (our lab network)
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
            continue;
        if (ifa->ifa_flags & IFF_LOOPBACK)
            continue;

        struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
        char addr_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sa->sin_addr, addr_str, sizeof(addr_str));

        if (strncmp(addr_str, "10.0.0.", 7) == 0) {
            best_addr = *sa;
            if (ifa->ifa_netmask)
                best_mask = *(struct sockaddr_in *)ifa->ifa_netmask;
            found = 1;
            break;
        }
    }

    // Second pass: any non-loopback AF_INET (fallback)
    if (!found) {
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
                continue;
            if (ifa->ifa_flags & IFF_LOOPBACK)
                continue;

            struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
            best_addr = *sa;
            if (ifa->ifa_netmask)
                best_mask = *(struct sockaddr_in *)ifa->ifa_netmask;
            found = 1;
            break;
        }
    }

    freeifaddrs(ifaddr);

    if (!found) {
        fprintf(stderr, "Could not find a non-loopback IPv4 address\n");
        exit(1);
    }

    // Compute broadcast = addr | ~netmask
    uint32_t addr = best_addr.sin_addr.s_addr;
    uint32_t mask = best_mask.sin_addr.s_addr;
    uint32_t bcast = addr | ~mask;

    struct in_addr ip_addr, bc_addr;
    ip_addr.s_addr = addr;
    bc_addr.s_addr = bcast;

    inet_ntop(AF_INET, &ip_addr, ip_out, ip_len);
    inet_ntop(AF_INET, &bc_addr, bcast_out, bcast_len);
}
void send_dv_if_needed(void)
{
    if (!isDvUpdated())
        return;

    pthread_mutex_lock(&neigh_mutex);

    for (int i = 0; i < neigh_count; i++)
    {
        const char *nbr = neigh_table[i].ip;

        // Build DV specifically for neighbor "nbr"
        char *dv_full = getDistanceVector();   // full DV string
        if (!dv_full) continue;

        // We will construct a split-horizon DV here:
        char dv_sh[4096];
        snprintf(dv_sh, sizeof(dv_sh), "%s:DV:", my_ip);

        // parse dv_full to filter routes
        char *saveptr;
        char *tok = strtok_r(dv_full, ":", &saveptr); // senderIP
        tok = strtok_r(NULL, ":", &saveptr);          // "DV"

        while ((tok = strtok_r(NULL, ":", &saveptr)) != NULL) {
            // tok is "(dest,cost)" 
            char dest[INET_ADDRSTRLEN];
            int cost;
            if (sscanf(tok, "(%15[^,],%d)", dest, &cost) != 2)
                continue;

            // SKIP if next-hop used to reach dest is this neighbor
            //
            // We check the route table directly:
            extern int getBestViaForDest(const char *dest, char *via_out);
            char via[INET_ADDRSTRLEN];
            if (getBestViaForDest(dest, via) && strcmp(via, nbr) == 0)
                continue;

            // otherwise, add it to the DV string
            char entry[128];
            snprintf(entry, sizeof(entry), "(%s,%d):", dest, cost);
            strncat(dv_sh, entry, sizeof(dv_sh) - strlen(dv_sh) - 1);
        }

        free(dv_full);

        // Now send this DV to the specific neighbor only
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(PORT);
        addr.sin_addr.s_addr = inet_addr(nbr);

        if (sendto(sockfd, dv_sh, strlen(dv_sh), 0,
                   (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            perror("sendto DV split horizon");
        } else {
            printf("DV to %s: %s\n", nbr, dv_sh);
        }
    }

    dvSent();
    pthread_mutex_unlock(&neigh_mutex);
}

// ===================================
// RECEIVER THREAD
// ===================================

void* receiver_thread(void *arg)
{
    (void)arg;
    while (1)
    {
        char buf[BUF_SIZE];
        struct sockaddr_in src;
        socklen_t srclen = sizeof(src);

        ssize_t n = recvfrom(sockfd, buf, sizeof(buf)-1, 0,
                             (struct sockaddr *)&src, &srclen);

        if (n < 0) { perror("recvfrom"); continue; }

        buf[n] = '\0';

        char src_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &src.sin_addr, src_ip, sizeof(src_ip));

        if (strcmp(src_ip, my_ip) == 0)
            continue;

        if (strstr(buf, ":DV:")) {
            processDistanceVector(buf);
            send_dv_if_needed();
            continue;
        }

        char *colon = strchr(buf, ':');
        if (!colon) continue;

        char *token = colon + 1;
        if (strncmp(token, "HELLO:", 6) != 0) continue;

        uint16_t netseq;
        memcpy(&netseq, token + 6, 2);
        uint16_t seq = ntohs(netseq);

        add_or_update_neighbor(src_ip, seq);

        updateRoute(src_ip, src_ip, 1);
        send_dv_if_needed();
    }
    return NULL;
}

// ===================================
// TIMEOUT THREAD
// ===================================

void* timeout_thread(void *arg)
{
    (void)arg;

    while (1) {
        time_t now = time(NULL);

        pthread_mutex_lock(&neigh_mutex);
        for (int i = 0; i < neigh_count; ) {
            if (difftime(now, neigh_table[i].last_time) > TIMEOUT) {

                char dead_ip[INET_ADDRSTRLEN];
                strncpy(dead_ip, neigh_table[i].ip, sizeof(dead_ip));
                printf("Neighbor %s timed out\n", dead_ip);

                neigh_table[i] = neigh_table[--neigh_count];

                pthread_mutex_unlock(&neigh_mutex);
                removeAllRoutesVia(dead_ip);
                send_dv_if_needed();
                pthread_mutex_lock(&neigh_mutex);
                continue;
            }
            i++;
        }
        pthread_mutex_unlock(&neigh_mutex);

        sleep(1);
    }
}

// ===================================
// AUTO-DETECT IP
// ===================================

// void detect_ip(void)
// {
//     struct ifaddrs *ifaddr, *ifa;
//     getifaddrs(&ifaddr);

//     for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
//         if (ifa->ifa_addr &&
//             ifa->ifa_addr->sa_family == AF_INET &&
//             !(ifa->ifa_flags & IFF_LOOPBACK))
//         {
//             struct sockaddr_in *sa = (struct sockaddr_in*)ifa->ifa_addr;
//             inet_ntop(AF_INET, &sa->sin_addr, my_ip, sizeof(my_ip));
//             break;
//         }
//     }

//     freeifaddrs(ifaddr);

//     printf("Local IP = %s\n", my_ip);
//     compute_broadcast_from_ip(my_ip);   // <-- WSL2 FIX
// }

// // ===================================
// // MAIN
// // ===================================


int main(void)
{
    // Pick correct interface (namespace or host veth)
    choose_local_ip_and_broadcast(my_ip, sizeof(my_ip),
                                  broadcast_ip, sizeof(broadcast_ip));

    printf("Local IP = %s\n", my_ip);
    printf("Computed broadcast address = %s\n", broadcast_ip);

    set_my_ip(my_ip);

    // ---- create socket ----
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) { perror("socket"); exit(1); }

    int yes = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(PORT);

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }

    // ---- start threads ----
    pthread_t th1, th2, th3;
    pthread_create(&th1, NULL, sender_thread, NULL);
    pthread_create(&th2, NULL, receiver_thread, NULL);
    pthread_create(&th3, NULL, timeout_thread, NULL);

    pthread_join(th1, NULL);
    pthread_join(th2, NULL);
    pthread_join(th3, NULL);

    close(sockfd);
    return 0;
}
