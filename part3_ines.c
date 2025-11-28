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

#define HELLO_INTERVAL 5
#define TIMEOUT 10
#define PORT 5555
#define BUF_SIZE 1024

extern void set_my_ip(const char *ip);
extern char* getDistanceVector(void);
extern void processDistanceVector(char *msg);
extern void dvSent(void);
extern int isDvUpdated(void);
extern void updateRoute(const char *dest, const char *via, int cost);
extern void removeAllRoutesVia(const char *via);

typedef struct {
    char ip[INET_ADDRSTRLEN];
    uint16_t last_seq;
    time_t last_time;
} Neighbor;

static Neighbor *neigh_table= NULL;
static int neigh_count= 0;
static int neigh_capacity= 0;

static pthread_mutex_t neigh_mutex = PTHREAD_MUTEX_INITIALIZER;

static char my_ip[INET_ADDRSTRLEN] = {0};
static uint16_t my_seq = 0;
static int sockfd;

void add_or_update_neighbor(const char *ip, uint16_t seq) {
    pthread_mutex_lock(&neigh_mutex);

    for (int i = 0; i < neigh_count; i++) {
        if (strcmp(neigh_table[i].ip, ip) == 0) {
            if (seq > neigh_table[i].last_seq) {
                neigh_table[i].last_seq = seq;
                neigh_table[i].last_time = time(NULL);
            }
            pthread_mutex_unlock(&neigh_mutex);
            return;
        }
    }

    if (neigh_count == neigh_capacity) {
        neigh_capacity = neigh_capacity == 0 ? 4 : neigh_capacity * 2;
        neigh_table = realloc(neigh_table, neigh_capacity * sizeof(Neighbor));
    }

    strcpy(neigh_table[neigh_count].ip, ip);
    neigh_table[neigh_count].last_seq = seq;
    neigh_table[neigh_count].last_time = time(NULL);
    neigh_count++;

    pthread_mutex_unlock(&neigh_mutex);
}

void* sender_thread(void *arg) {
    (void)arg;

    struct sockaddr_in bcast_addr;
    memset(&bcast_addr, 0, sizeof(bcast_addr));
    bcast_addr.sin_family = AF_INET;
    bcast_addr.sin_port = htons(PORT);
    bcast_addr.sin_addr.s_addr = inet_addr("255.255.255.255");

    while (1) {
        char msg[BUF_SIZE];
        int len = snprintf(msg, sizeof(msg), "%s:HELLO:", my_ip);

        uint16_t netseq = htons(my_seq);
        memcpy(msg + len, &netseq, sizeof(netseq));
        len += sizeof(netseq);

        sendto(sockfd, msg, len, 0, (struct sockaddr*)&bcast_addr, sizeof(bcast_addr));
        my_seq++;

        sleep(HELLO_INTERVAL);
    }
}

void send_dv_if_needed(void) {
    if (!isDvUpdated())
        return;

    char *dv = getDistanceVector();
    if (!dv) return;

    struct sockaddr_in bcast;
    memset(&bcast, 0, sizeof(bcast));
    bcast.sin_family= AF_INET;
    bcast.sin_port= htons(PORT);
    bcast.sin_addr.s_addr=inet_addr("255.255.255.255");

    sendto(sockfd, dv, strlen(dv), 0, (struct sockaddr*)&bcast, sizeof(bcast));
    dvSent();
    free(dv);
}

void* receiver_thread(void *arg) {
    (void)arg;

    while (1) {
        char buf[BUF_SIZE];
        struct sockaddr_in src;
        socklen_t srclen = sizeof(src);

        ssize_t n = recvfrom(sockfd, buf, sizeof(buf), 0, (struct sockaddr*)&src, &srclen);
        if (n < 0) continue;

        char src_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &src.sin_addr, src_ip, sizeof(src_ip));

        if (strcmp(src_ip, my_ip) == 0)
            continue;

        buf[n] = '\0';

        if (strstr(buf, ":DV:")) {
            processDistanceVector(buf);
            send_dv_if_needed();
            continue;
        }

        char *colon = strchr(buf, ':');
        if (!colon) continue;

        char *token = colon + 1;
        if (strncmp(token, "HELLO:", 6) != 0) continue;

        char *seq_ptr = token + 6;

        uint16_t netseq;
        memcpy(&netseq, seq_ptr, sizeof(netseq));
        uint16_t seq = ntohs(netseq);

        add_or_update_neighbor(src_ip, seq);

        updateRoute(src_ip, src_ip, 1);
        send_dv_if_needed();
    }
}

void* timeout_thread(void *arg) {
    (void)arg;

    while (1) {
        time_t now = time(NULL);

        pthread_mutex_lock(&neigh_mutex);
        for (int i = 0; i < neigh_count; ) {
            if (difftime(now, neigh_table[i].last_time) > TIMEOUT) {
                char down_ip[INET_ADDRSTRLEN];
                strcpy(down_ip, neigh_table[i].ip);

                neigh_table[i] = neigh_table[--neigh_count];
                pthread_mutex_unlock(&neigh_mutex);

                removeAllRoutesVia(down_ip);
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

void detect_ip(void) {
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
}

int main(void) {
    detect_ip();
    set_my_ip(my_ip);

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) exit(1);

    int broadcastEnable = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family= AF_INET;
    addr.sin_addr.s_addr= htonl(INADDR_ANY);
    addr.sin_port= htons(PORT);

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
        exit(1);

    pthread_t th1, th2, th3;
    pthread_create(&th1,NULL, sender_thread,NULL);
    pthread_create(&th2,NULL, receiver_thread,NULL);
    pthread_create(&th3,NULL, timeout_thread,NULL);

    pthread_join(th1,NULL);
    pthread_join(th2,NULL);
    pthread_join(th3,NULL);

    close(sockfd);
    return 0;
}
