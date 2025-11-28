/*
 * Part 2: Distance Table and Distance Vector processing
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <arpa/inet.h>

#define INITIAL_DEST_CAP 4
#define INITIAL_ROUTE_CAP 4
#define DV_BUF_SIZE 4096   /* Max size for serialized DV */

/* ---------------- Structures ---------------- */

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

/* ---------------- Globals ---------------- */

static DestEntry *distTable = NULL;
static int distCount = 0;
static int distCap   = 0;

static int updatedDV = 0;

static pthread_mutex_t dist_mutex = PTHREAD_MUTEX_INITIALIZER;

static char my_ip[INET_ADDRSTRLEN] = "0.0.0.0";

/* ---------------- Forward declarations ---------------- */

static int findDestIndex(const char *dest);
static int getBestCost(int destIndex);
static int findRouteIndex(int destIndex, const char *via);

int getBestViaForDest(const char *dest, char *via_out);  // for split horizon

void dvUpdate(void);
void dvSent(void);
void updateRoute(const char *dest, const char *via, int cost);
void removeAllRoutesVia(const char *via);
char* getDistanceVector(void);
void processDistanceVector(char *msg);
void printDistanceTable(void);

/* ---------------- set_my_ip ---------------- */

void set_my_ip(const char *ip) {
    if (ip) {
        strncpy(my_ip, ip, sizeof(my_ip));
        my_ip[sizeof(my_ip)-1] = '\0';
    }
}

/* ---------------- Helper: find destination index ---------------- */

static int findDestIndex(const char *dest) {
    for (int i = 0; i < distCount; i++) {
        if (strcmp(distTable[i].dest, dest) == 0)
            return i;
    }
    return -1;
}

/* ---------------- Helper: find route index ---------------- */

static int findRouteIndex(int destIndex, const char *via) {
    DestEntry *d = &distTable[destIndex];
    for (int i = 0; i < d->routeCount; i++) {
        if (strcmp(d->routes[i].via, via) == 0)
            return i;
    }
    return -1;
}

/* ---------------- Helper: best cost ---------------- */

static int getBestCost(int destIndex) {
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

/* ---------------- Ensure capacity ---------------- */

static void ensureDistCapacity(void) {
    if (distCount < distCap)
        return;

    int newCap = (distCap == 0 ? INITIAL_DEST_CAP : distCap * 2);
    DestEntry *tmp = realloc(distTable, newCap * sizeof(DestEntry));
    if (!tmp) {
        perror("realloc distTable");
        exit(1);
    }
    distTable = tmp;
    distCap   = newCap;
}

static void ensureRouteCapacity(DestEntry *d) {
    if (d->routeCount < d->routeCap)
        return;

    int newCap = (d->routeCap == 0 ? INITIAL_ROUTE_CAP : d->routeCap * 2);
    RouteVia *tmp = realloc(d->routes, newCap * sizeof(RouteVia));
    if (!tmp) {
        perror("realloc routes");
        exit(1);
    }
    d->routes = tmp;
    d->routeCap = newCap;
}

/* ---------------- updateRoute ---------------- */

void updateRoute(const char *dest, const char *via, int cost) {
    if (!dest || !via)
        return;

    pthread_mutex_lock(&dist_mutex);

    int idx = findDestIndex(dest);
    if (idx < 0) {
        ensureDistCapacity();
        idx = distCount++;
        DestEntry *d = &distTable[idx];

        strncpy(d->dest, dest, sizeof(d->dest));
        d->dest[sizeof(d->dest)-1] = '\0';

        d->routes     = NULL;
        d->routeCount = 0;
        d->routeCap   = 0;
    }

    DestEntry *d = &distTable[idx];
    int oldBest = getBestCost(idx);

    int rIdx = findRouteIndex(idx, via);
    if (rIdx < 0) {
        ensureRouteCapacity(d);
        rIdx = d->routeCount++;
        strncpy(d->routes[rIdx].via, via, sizeof(d->routes[rIdx].via));
        d->routes[rIdx].via[sizeof(d->routes[rIdx].via)-1] = '\0';
    }

    d->routes[rIdx].cost = cost;

    int newBest = getBestCost(idx);
    if (newBest != oldBest)
        dvUpdate();

    pthread_mutex_unlock(&dist_mutex);
}

/* ---------------- removeAllRoutesVia ---------------- */

void removeAllRoutesVia(const char *via) {
    if (!via) return;

    pthread_mutex_lock(&dist_mutex);

    for (int i = 0; i < distCount; ) {
        DestEntry *d = &distTable[i];
        int oldBest = getBestCost(i);

        for (int r = 0; r < d->routeCount; ) {
            if (strcmp(d->routes[r].via, via) == 0)
                d->routes[r] = d->routes[--d->routeCount];
            else
                r++;
        }

        if (d->routeCount == 0) {
            free(d->routes);
            distTable[i] = distTable[--distCount];
            continue;
        }

        int newBest = getBestCost(i);
        if (newBest != oldBest)
            dvUpdate();

        i++;
    }

    pthread_mutex_unlock(&dist_mutex);
}

/* ---------------- getDistanceVector ---------------- */

char* getDistanceVector(void) {
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

        int w = snprintf(buf+offset, DV_BUF_SIZE-offset, "(%s,%d):",
                         distTable[i].dest, best);

        offset += w;
        if (offset >= DV_BUF_SIZE)
            break;
    }

    pthread_mutex_unlock(&dist_mutex);
    return buf;
}

/* ---------------- processDistanceVector ---------------- */

void processDistanceVector(char *msg) {
    if (!msg) return;

    char *copy = strdup(msg);
    if (!copy) return;

    char sender[INET_ADDRSTRLEN] = {0};
    char *saveptr;

    char *tok = strtok_r(copy, ":", &saveptr);
    if (!tok) { free(copy); return; }

    strncpy(sender, tok, sizeof(sender));
    sender[sizeof(sender)-1] = '\0';

    if (strcmp(sender, my_ip) == 0) {
        free(copy);
        return;
    }

    tok = strtok_r(NULL, ":", &saveptr);
    if (!tok || strcmp(tok, "DV") != 0) {
        free(copy);
        return;
    }

    while ((tok = strtok_r(NULL, ":", &saveptr)) != NULL) {
        if (tok[0] != '(') continue;

        char dest[INET_ADDRSTRLEN];
        int dist;

        if (sscanf(tok, "(%15[^,],%d)", dest, &dist) != 2)
            continue;

        if (strcmp(dest, my_ip) == 0)
            continue;

        updateRoute(dest, sender, dist+1);
    }

    free(copy);
}

/* ---------------- dvUpdate / dvSent ---------------- */

void dvUpdate(void) { updatedDV = 1; }
void dvSent(void)   { updatedDV = 0; }

int isDvUpdated(void) { return updatedDV; }

/* ---------------- Split Horizon helper ---------------- */

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

/* ---------------- printDistanceTable (debug) ---------------- */

void printDistanceTable(void) {
    pthread_mutex_lock(&dist_mutex);

    printf("Distance table for %s:\n", my_ip);
    for (int i = 0; i < distCount; i++) {
        DestEntry *d = &distTable[i];
        printf("  %s:\n", d->dest);
        for (int r = 0; r < d->routeCount; r++)
            printf("    via %s cost %d\n", d->routes[r].via, d->routes[r].cost);
    }

    pthread_mutex_unlock(&dist_mutex);
}



/* ---------------- END OF FUNCTION DEFINITIONS ---------------- */

#ifdef TEST_PART2
int main(void) {
    set_my_ip("10.0.0.1");

    updateRoute("10.0.0.2", "10.0.0.2", 1);
    updateRoute("10.0.0.3", "10.0.0.2", 2);

    printf("Initial DT:\n");
    printDistanceTable();

    char *dv = getDistanceVector();
    printf("Generated: %s\n", dv);
    free(dv);

    char incoming[] = "10.0.0.2:DV:(10.0.0.3,1):(10.0.0.4,5):";
    processDistanceVector(incoming);

    printf("After DV:\n");
    printDistanceTable();
    return 0;
}
#endif
