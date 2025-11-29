/*
 * Part 2 BONUS: Distance Table + host Routing Table Integration
 *
 * This file is identical to your original part2.c,
 * BUT with the following extra features:
 *
 *   - Every time the BEST route to a destination changes,
 *       → we install/update a real Linux host route:
 *           ip route replace DEST/32 via NEXT_HOP
 *
 *   - Every time a destination loses all routes,
 *       → we delete the host route:
 *           ip route del DEST/32
 *
 *   - Nothing changes in the public API:
 *       set_my_ip()
 *       getDistanceVector()
 *       processDistanceVector()
 *       updateRoute()
 *       removeAllRoutesVia()
 *       dvUpdate(), dvSent(), isDvUpdated()
 *
 * IMPORTANT:
 *   You must run the router program as root:
 *      sudo ./router_bonus
 *
 *   host commands used:
 *      system("ip route replace ...");
 *      system("ip route del ...");
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <unistd.h>

void dvUpdate(void);
void dvSent(void);
int isDvUpdated(void);

#define INITIAL_DEST_CAP 4
#define INITIAL_ROUTE_CAP 4
#define DV_BUF_SIZE 4096   /* Max size for serialized DV */

/* --------------------- DATA STRUCTURES --------------------- */

typedef struct {
    char via[INET_ADDRSTRLEN];
    int  cost;
} RouteVia;

typedef struct {
    char     dest[INET_ADDRSTRLEN];
    RouteVia *routes;
    int      routeCount;
    int      routeCap;
} DestEntry;

static DestEntry *distTable = NULL;
static int distCount = 0;
static int distCap   = 0;

static int updatedDV = 0;

static pthread_mutex_t dist_mutex = PTHREAD_MUTEX_INITIALIZER;

static char my_ip[INET_ADDRSTRLEN] = "0.0.0.0";

/* Forward declarations */
static int findDestIndex(const char *dest);
static int findRouteIndex(int destIndex, const char *via);
static int getBestCost(int destIndex);
static const char* getBestVia(int destIndex);
int getBestViaForDest(const char *dest, char *via_out);  // for split horizon

/* --------------------- host ROUTE HELPERS (BONUS) --------------------- */

/* Apply: ip route replace DEST/32 via NEXT_HOP */
static void apply_host_route(const char *dest, const char *via)

{
    if (strcmp(dest, my_ip) == 0)
        return;

    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "ip route replace %s/32 via %s", dest, via);

    printf("[host] %s\n", cmd);
    fflush(stdout);

    int ret = system(cmd);
    if (ret == -1)
        perror("[host] system(ip route replace)");
}

/* Delete: ip route del DEST/32 */
static void delete_host_route(const char *dest)
{
    if (strcmp(dest, my_ip) == 0)
        return;

    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "ip route del %s/32", dest);

    printf("host %s\n", cmd);
    fflush(stdout);

    int ret = system(cmd);
    if (ret == -1)
        perror("host system(ip route del)");
}

/* --------------------- INTERNAL HELPERS --------------------- */

void set_my_ip(const char *ip)
{
    if (ip) {
        strncpy(my_ip, ip, sizeof(my_ip));
        my_ip[sizeof(my_ip) - 1] = '\0';
    }
}

static int findDestIndex(const char *dest)
{
    for (int i = 0; i < distCount; i++) {
        if (strcmp(distTable[i].dest, dest) == 0)
            return i;
    }
    return -1;
}

static int findRouteIndex(int destIndex, const char *via)
{
    DestEntry *d = &distTable[destIndex];
    for (int i = 0; i < d->routeCount; i++) {
        if (strcmp(d->routes[i].via, via) == 0)
            return i;
    }
    return -1;
}

static int getBestCost(int destIndex)
{
    DestEntry *d = &distTable[destIndex];
    if (d->routeCount == 0)
        return -1;

    int best = d->routes[0].cost;
    for (int i = 1; i < d->routeCount; i++) {
        if (d->routes[i].cost < best)
            best = d->routes[i].cost;
    }
    return best;
}

/* NEW: best next-hop neighbor */
static const char* getBestVia(int destIndex)
{
    DestEntry *d = &distTable[destIndex];
    if (d->routeCount == 0)
        return NULL;

    int best = d->routes[0].cost;
    const char *bestVia = d->routes[0].via;

    for (int i = 1; i < d->routeCount; i++) {
        if (d->routes[i].cost < best) {
            best = d->routes[i].cost;
            bestVia = d->routes[i].via;
        }
    }
    return bestVia;
}

static void ensureDistCapacity(void)
{
    if (distCount < distCap)
        return;

    int newCap = (distCap == 0) ? INITIAL_DEST_CAP : distCap * 2;
    DestEntry *tmp = realloc(distTable, newCap * sizeof(DestEntry));
    if (!tmp) {
        perror("realloc distTable");
        exit(1);
    }
    distTable = tmp;
    distCap   = newCap;
}

static void ensureRouteCapacity(DestEntry *d)
{
    if (d->routeCount < d->routeCap)
        return;

    int newCap = (d->routeCap == 0) ? INITIAL_ROUTE_CAP : d->routeCap * 2;
    RouteVia *tmp = realloc(d->routes, newCap * sizeof(RouteVia));
    if (!tmp) {
        perror("realloc routes");
        exit(1);
    }
    d->routes   = tmp;
    d->routeCap = newCap;
}

/* --------------------- CORE FUNCTIONS + BONUS LOGIC --------------------- */

/*
 * updateRoute (BONUS):
 *   original logic + host route updates
 */
void updateRoute(const char *dest, const char *via, int cost)
{
    if (!dest || !via)
        return;

    pthread_mutex_lock(&dist_mutex);

    int idx = findDestIndex(dest);
    if (idx < 0) {
        ensureDistCapacity();
        idx = distCount++;
        DestEntry *d = &distTable[idx];
        strncpy(d->dest, dest, sizeof(d->dest));
        d->dest[sizeof(d->dest) - 1] = '\0';
        d->routes = NULL;
        d->routeCount = 0;
        d->routeCap   = 0;
    }

    DestEntry *d = &distTable[idx];

    int oldBestCost = getBestCost(idx);
    const char *oldBestVia = getBestVia(idx);

    int rIdx = findRouteIndex(idx, via);
    if (rIdx < 0) {
        ensureRouteCapacity(d);
        rIdx = d->routeCount++;
        strncpy(d->routes[rIdx].via, via, sizeof(d->routes[rIdx].via));
        d->routes[rIdx].via[INET_ADDRSTRLEN - 1] = '\0';
    }

    d->routes[rIdx].cost = cost;

    int newBestCost = getBestCost(idx);
    const char *newBestVia = getBestVia(idx);

    /* Did the best route change? */
    if (newBestCost != oldBestCost ||
        (oldBestVia && newBestVia && strcmp(oldBestVia, newBestVia) != 0))
    {
        dvUpdate();

        /* BONUS: update host route */
        if (newBestVia && newBestCost >= 0) {
            apply_host_route(dest, newBestVia);

        }
    }

    pthread_mutex_unlock(&dist_mutex);
}

/*
 * removeAllRoutesVia (BONUS):
 *   original logic + host route deletion
 */
void removeAllRoutesVia(const char *via)
{
    if (!via) return;

    pthread_mutex_lock(&dist_mutex);

    for (int i = 0; i < distCount; ) {
        DestEntry *d = &distTable[i];

        int oldBestCost = getBestCost(i);
        const char *oldBestVia = getBestVia(i);

        /* Remove routes via this neighbor */
        for (int r = 0; r < d->routeCount; ) {
            if (strcmp(d->routes[r].via, via) == 0) {
                d->routes[r] = d->routes[--d->routeCount];
            } else {
                r++;
            }
        }

        /* No routes left → remove destination + delete host route */
        if (d->routeCount == 0) {
            delete_host_route(d->dest);

            free(d->routes);
            distTable[i] = distTable[--distCount];
            continue;
        }

        /* Check if best route changed */
        int newBestCost = getBestCost(i);
        const char *newBestVia = getBestVia(i);

        if (newBestCost != oldBestCost ||
            (oldBestVia && newBestVia && strcmp(oldBestVia, newBestVia) != 0))
        {
            dvUpdate();

            /* install new best host route */
            if (newBestVia && newBestCost >= 0) {
                apply_host_route(d->dest, newBestVia);
            }
        }

        i++;
    }

    pthread_mutex_unlock(&dist_mutex);
}

/* --------------------- DISTANCE VECTOR LOGIC (unchanged) --------------------- */

char* getDistanceVector(void)
{
    pthread_mutex_lock(&dist_mutex);

    char *buf = malloc(DV_BUF_SIZE);
    if (!buf) {
        pthread_mutex_unlock(&dist_mutex);
        return NULL;
    }

    int offset = snprintf(buf, DV_BUF_SIZE, "%s:DV:", my_ip);

    for (int i = 0; i < distCount; i++) {
        int best = getBestCost(i);
        if (best < 0) continue;

        offset += snprintf(buf + offset, DV_BUF_SIZE - offset,
                           "(%s,%d):", distTable[i].dest, best);
    }

    pthread_mutex_unlock(&dist_mutex);
    return buf;
}

void processDistanceVector(char *msg)
{
    if (!msg) return;

    char *copy = strdup(msg);
    if (!copy) return;

    char sender[INET_ADDRSTRLEN] = {0};
    char *saveptr = NULL;

    char *token = strtok_r(copy, ":", &saveptr);
    if (!token) { free(copy); return; }
    strncpy(sender, token, sizeof(sender));

    /* Ignore our own DV */
    if (strcmp(sender, my_ip) == 0) {
        free(copy);
        return;
    }

    token = strtok_r(NULL, ":", &saveptr);
    if (!token || strcmp(token, "DV") != 0) {
        free(copy);
        return;
    }

    while ((token = strtok_r(NULL, ":", &saveptr)) != NULL) {
        char dest[INET_ADDRSTRLEN];
        int dist;
        if (sscanf(token, "(%15[^,],%d)", dest, &dist) != 2)
            continue;

        if (strcmp(dest, my_ip) == 0)
            continue;

        updateRoute(dest, sender, dist + 1);
    }

    free(copy);
}

void dvUpdate(void)
{
    updatedDV = 1;
}

void dvSent(void)
{
    updatedDV = 0;
}

int isDvUpdated(void)
{
    return updatedDV;
}

/* --------------------- BEST VIA HELPER (for Split Horizon) --------------------- */
/* 
 * Returns 1 and writes best next-hop into via_out
 * Returns 0 if no route exists to dest
 */
int getBestViaForDest(const char *dest, char *via_out)
{
    pthread_mutex_lock(&dist_mutex);

    int idx = findDestIndex(dest);
    if (idx < 0) {
        pthread_mutex_unlock(&dist_mutex);
        return 0;
    }

    DestEntry *d = &distTable[idx];
    if (d->routeCount == 0) {
        pthread_mutex_unlock(&dist_mutex);
        return 0;
    }

    int best = d->routes[0].cost;
    const char *bestVia = d->routes[0].via;

    for (int i = 1; i < d->routeCount; i++) {
        if (d->routes[i].cost < best) {
            best    = d->routes[i].cost;
            bestVia = d->routes[i].via;
        }
    }

    strncpy(via_out, bestVia, INET_ADDRSTRLEN);

    pthread_mutex_unlock(&dist_mutex);
    return 1;
}

/* Optional, not modified */
void clearDistanceTable(void)
{
    pthread_mutex_lock(&dist_mutex);
    for (int i = 0; i < distCount; i++)
        free(distTable[i].routes);
    free(distTable);
    distTable = NULL;
    distCount = 0;
    distCap = 0;
    pthread_mutex_unlock(&dist_mutex);
}

void printDistanceTable(void)
{
    pthread_mutex_lock(&dist_mutex);

    printf("Distance table for %s:\n", my_ip);
    for (int i = 0; i < distCount; i++) {
        DestEntry *d = &distTable[i];
        printf("  Dest %s:\n", d->dest);
        for (int r = 0; r < d->routeCount; r++) {
            printf("    via %s cost %d\n",
                   d->routes[r].via,
                   d->routes[r].cost);
        }
    }

    pthread_mutex_unlock(&dist_mutex);
}
