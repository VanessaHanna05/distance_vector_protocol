/*
 * Part 2: Distance Table and Distance Vector processing
 *
 * This file implements:
 *
 *   - A distance table data structure:
 *       for each destination: a list of (via_neighbor, cost)
 *
 *   - The required API:
 *       char* getDistanceVector(void);
 *       void  processDistanceVector(char *msg);
 *
 *   - The two integration hooks:
 *       void dvUpdate(void);
 *       void dvSent(void);
 *
 *   - Extra helpers:
 *       void set_my_ip(const char *ip);
 *       void updateRoute(const char *dest, const char *via, int cost);
 *       void removeAllRoutesVia(const char *via);
 *       void printDistanceTable(void);
 *
 * The DV encoding is:
 *   senderIPAddress:DV:(dest1,dist1):(dest2,dist2):...:
 *
 * Example:
 *   10.0.0.1:DV:(10.0.0.2,1):(10.0.0.3,2):
 *
 * getDistanceVector returns a malloc'ed string.
 * The caller must free() it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <arpa/inet.h>

#define INITIAL_DEST_CAP 4
#define INITIAL_ROUTE_CAP 4
#define DV_BUF_SIZE 4096   /* Max size for serialized DV */

/* A single route to a destination via a specific neighbor */
typedef struct {
    char via[INET_ADDRSTRLEN];  /* IP of neighbor used as next hop */
    int  cost;                  /* hop count via that neighbor */
} RouteVia;

/* A destination entry in the distance table */
typedef struct {
    char     dest[INET_ADDRSTRLEN]; /* destination IP */
    RouteVia *routes;               /* dynamic array of routes for this dest */
    int      routeCount;            /* how many RouteVia currently used */
    int      routeCap;              /* allocated capacity */
} DestEntry;

/* Global distance table */
static DestEntry *distTable = NULL;
static int distCount = 0;
static int distCap   = 0;

/* Whether the DV has changed in a way that affects best distances */
static int updatedDV = 0;

/* Mutex to protect distTable and updatedDV */
static pthread_mutex_t dist_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Local router IP, to be set by caller */
static char my_ip[INET_ADDRSTRLEN] = "0.0.0.0";

/* Forward declarations */
void dvUpdate(void);
void dvSent(void);
void updateRoute(const char *dest, const char *via, int cost);
void removeAllRoutesVia(const char *via);
char* getDistanceVector(void);
void processDistanceVector(char *msg);

/* Helper for tests, to print the table */
void printDistanceTable(void);

/* Helper: set my_ip from outside */
void set_my_ip(const char *ip) {
    if (ip) {
        strncpy(my_ip, ip, sizeof(my_ip));
        my_ip[sizeof(my_ip) - 1] = '\0';
    }
}

/* Helper: find destination index in distTable, or -1 if not found */
static int findDestIndex(const char *dest) {
    for (int i = 0; i < distCount; i++) {
        if (strcmp(distTable[i].dest, dest) == 0) {
            return i;
        }
    }
    return -1;
}

/* Helper: find index of a route via given neighbor for a dest index, or -1 */
static int findRouteIndex(int destIndex, const char *via) {
    DestEntry *d = &distTable[destIndex];
    for (int i = 0; i < d->routeCount; i++) {
        if (strcmp(d->routes[i].via, via) == 0) {
            return i;
        }
    }
    return -1;
}

/* Helper: return best cost (minimum cost) for destination index.
 * Returns -1 if there is no route.
 */
static int getBestCost(int destIndex) {
    DestEntry *d = &distTable[destIndex];
    if (d->routeCount == 0) {
        return -1;
    }
    int best = d->routes[0].cost;
    for (int i = 1; i < d->routeCount; i++) {
        if (d->routes[i].cost < best) {
            best = d->routes[i].cost;
        }
    }
    return best;
}

/* Ensure that distTable has capacity for at least one more destination */
static void ensureDistCapacity(void) {
    if (distCount < distCap) {
        return;
    }
    int newCap = (distCap == 0) ? INITIAL_DEST_CAP : distCap * 2;
    DestEntry *tmp = realloc(distTable, newCap * sizeof(DestEntry));
    if (!tmp) {
        perror("realloc distTable");
        exit(1);
    }
    distTable = tmp;
    distCap   = newCap;
}

/* Ensure that a destination entry has capacity for at least one more route */
static void ensureRouteCapacity(DestEntry *d) {
    if (d->routeCount < d->routeCap) {
        return;
    }
    int newCap = (d->routeCap == 0) ? INITIAL_ROUTE_CAP : d->routeCap * 2;
    RouteVia *tmp = realloc(d->routes, newCap * sizeof(RouteVia));
    if (!tmp) {
        perror("realloc routes");
        exit(1);
    }
    d->routes    = tmp;
    d->routeCap  = newCap;
}

/*
 * updateRoute:
 *   Update the distance table so that:
 *      distance to "dest" via "via" = cost.
 *   This can:
 *      - create a new destination entry
 *      - create a new route via this neighbor
 *      - update an existing route
 *
 *   If the best (shortest) cost for dest changes, dvUpdate() is called,
 *   which sets updatedDV = 1.
 */
void updateRoute(const char *dest, const char *via, int cost) {
    if (!dest || !via) {
        return;
    }

    pthread_mutex_lock(&dist_mutex);

    /* Find or create destination entry */
    int idx = findDestIndex(dest);
    if (idx < 0) {
        ensureDistCapacity();
        idx = distCount++;
        DestEntry *d = &distTable[idx];
        strncpy(d->dest, dest, sizeof(d->dest));
        d->dest[sizeof(d->dest) - 1] = '\0';
        d->routes     = NULL;
        d->routeCount = 0;
        d->routeCap   = 0;
    }

    DestEntry *d = &distTable[idx];

    int oldBest = getBestCost(idx);

    /* Find or create route via "via" */
    int rIdx = findRouteIndex(idx, via);
    if (rIdx < 0) {
        ensureRouteCapacity(d);
        rIdx = d->routeCount++;
        strncpy(d->routes[rIdx].via, via, sizeof(d->routes[rIdx].via));
        d->routes[rIdx].via[sizeof(d->routes[rIdx].via) - 1] = '\0';
    }

    d->routes[rIdx].cost = cost;

    int newBest = getBestCost(idx);
    if (newBest != oldBest) {
        dvUpdate();
    }

    pthread_mutex_unlock(&dist_mutex);
}

/*
 * removeAllRoutesVia:
 *   Remove every route that uses "via" as the next hop.
 *   If a destination ends up with zero routes, remove that destination.
 */
void removeAllRoutesVia(const char *via) {
    if (!via) return;

    pthread_mutex_lock(&dist_mutex);

    for (int i = 0; i < distCount; ) {
        DestEntry *d = &distTable[i];

        int oldBest = getBestCost(i);

        /* Remove routes in this dest that use "via" */
        for (int r = 0; r < d->routeCount; ) {
            if (strcmp(d->routes[r].via, via) == 0) {
                d->routes[r] = d->routes[--d->routeCount];
            } else {
                r++;
            }
        }

        /* If no more routes, remove destination altogether */
        if (d->routeCount == 0) {
            free(d->routes);
            distTable[i] = distTable[--distCount];
            continue; /* do not increment i, check new entry at i */
        }

        int newBest = getBestCost(i);
        if (newBest != oldBest) {
            dvUpdate();
        }

        i++;
    }

    pthread_mutex_unlock(&dist_mutex);
}

/*
 * getDistanceVector:
 *   Build and return a malloc'ed string containing the DV, in the format:
 *
 *     my_ip:DV:(dest1,dist1):(dest2,dist2):...:
 *
 *   We use the best (shortest) cost for each destination.
 *   The caller must free() the returned string.
 */
char* getDistanceVector(void) {
    pthread_mutex_lock(&dist_mutex);

    char *buf = malloc(DV_BUF_SIZE);
    if (!buf) {
        pthread_mutex_unlock(&dist_mutex);
        return NULL;
    }
    buf[0] = '\0';

    int offset = snprintf(buf, DV_BUF_SIZE, "%s:DV:", my_ip);
    if (offset < 0 || offset >= DV_BUF_SIZE) {
        /* Something went wrong or message is too big */
        pthread_mutex_unlock(&dist_mutex);
        free(buf);
        return NULL;
    }

    for (int i = 0; i < distCount; i++) {
        int best = getBestCost(i);
        if (best < 0) {
            continue;
        }

        int written = snprintf(buf + offset,
                               DV_BUF_SIZE - offset,
                               "(%s,%d):",
                               distTable[i].dest,
                               best);
        if (written < 0 || written >= DV_BUF_SIZE - offset) {
            /* Not enough space, truncate and stop */
            break;
        }
        offset += written;
    }

    pthread_mutex_unlock(&dist_mutex);
    return buf;
}

/*
 * processDistanceVector:
 *   Parse an incoming DV string and update the distance table.
 *
 *   Expected format:
 *       senderIP:DV:(dest1,dist1):(dest2,dist2):...:
 *
 *   For each (dest,dist) we set:
 *       distance to dest via senderIP = dist + 1
 *
 *   If this changes any best distance, dvUpdate() will be called
 *   inside updateRoute().
 *
 *   Note: we do not modify the caller's string. We copy it first.
 */
void processDistanceVector(char *msg) {
    if (!msg) return;

    /* Make a copy so we can safely use strtok */
    char *copy = strdup(msg);
    if (!copy) return;

    char sender[INET_ADDRSTRLEN] = {0};
    char *saveptr = NULL;

    /* First token: senderIP */
    char *token = strtok_r(copy, ":", &saveptr);
    if (!token) {
        free(copy);
        return;
    }
    strncpy(sender, token, sizeof(sender));
    sender[sizeof(sender) - 1] = '\0';

    /* If the DV is from ourselves, ignore */
    if (strcmp(sender, my_ip) == 0) {
        free(copy);
        return;
    }

    /* Second token: should be "DV" */
    token = strtok_r(NULL, ":", &saveptr);
    if (!token || strcmp(token, "DV") != 0) {
        free(copy);
        return;
    }

    /* Remaining tokens are of form "(dest,dist)" one per ":" group */
    while ((token = strtok_r(NULL, ":", &saveptr)) != NULL) {
        if (token[0] != '(') {
            continue;
        }

        char dest[INET_ADDRSTRLEN] = {0};
        int dist = 0;

        /* Parse "(dest,dist)" */
        if (sscanf(token, "(%15[^,],%d)", dest, &dist) != 2) {
            continue;
        }

        /* We can choose to ignore entries that claim a route to ourselves,
         * or we could store them. Ignoring is simpler and avoids confusion.
         */
        if (strcmp(dest, my_ip) == 0) {
            continue;
        }

        int newCost = dist + 1;
        if (newCost < 1) {
            /* Sanity, distance should be positive */
            newCost = 1;
        }

        /* Update route dest via sender with newCost */
        updateRoute(dest, sender, newCost);
    }

    free(copy);
}

/*
 * dvUpdate:
 *   Mark that the DV has changed in a way that affects shortest paths.
 *   The actual sending of the DV will be handled in Part 3.
 */
void dvUpdate(void) {
    updatedDV = 1;
}

/*
 * dvSent:
 *   Called whenever a DV has just been sent.
 *   Resets the updatedDV flag.
 */
void dvSent(void) {
    updatedDV = 0;
}

/*
 * Optional helper to check if updatedDV is set.
 * Could be used by a DV sender thread later.
 */
int isDvUpdated(void) {
    return updatedDV;
}

/*
 * Optional helper to free all distance table memory.
 * Call at program exit if needed.
 */
void clearDistanceTable(void) {
    pthread_mutex_lock(&dist_mutex);
    for (int i = 0; i < distCount; i++) {
        free(distTable[i].routes);
    }
    free(distTable);
    distTable = NULL;
    distCount = 0;
    distCap   = 0;
    pthread_mutex_unlock(&dist_mutex);
}

/*
 * Helper to print the current distance table.
 * Useful for testing Part 2 without networking.
 */
void printDistanceTable(void) {
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

/* 
 * Optional simple main() to test Part 2 only.
 * You can remove this when integrating into your main project.
 *
 * Compile:
 *   gcc -Wall -Wextra part2.c -o part2
 *
 * Example run:
 *   ./part2
 */
#ifdef TEST_PART2
int main(void) {
    set_my_ip("10.0.0.1");

    /* Pretend we learned these routes manually */
    updateRoute("10.0.0.2", "10.0.0.2", 1);
    updateRoute("10.0.0.3", "10.0.0.2", 2);

    printDistanceTable();

    char *dv = getDistanceVector();
    if (dv) {
        printf("Generated DV: %s\n", dv);
    }

    /* Now pretend we receive a DV from 10.0.0.2 */
    char incoming[] = "10.0.0.2:DV:(10.0.0.3,1):(10.0.0.4,3):";
    processDistanceVector(incoming);

    printDistanceTable();

    free(dv);
    clearDistanceTable();
    return 0;
}
#endif
