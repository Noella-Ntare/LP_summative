/*
 * server.c
 *
 * Concurrent TCP server for a university lab equipment booking system.
 *
 * Protocol (simple line-based text protocol over TCP):
 *   Client -> Server : AUTH <user_id>\n
 *   Server -> Client : AUTH_OK\n<equipment list, one per line>\nEND\n
 *                      or  AUTH_FAIL\n   (then server closes the connection)
 *   Client -> Server : RESERVE <equipment_name>\n
 *   Server -> Client : RESERVE_OK <equipment_name>\n
 *                      or  RESERVE_FAIL <equipment_name> <reason>\n
 *   Client -> Server : QUIT\n
 *   Server -> Client : BYE <user_id>\n            (then closes connection)
 *
 * Concurrency model: one thread per connected client (POSIX threads).
 * Shared state (registered users' online/offline status is implicit;
 * the equipment reservation table) is protected by a single mutex,
 * so two clients can never reserve the same equipment at the same time.
 *
 * Build: gcc -Wall -O2 -pthread -o server server.c
 * Run:   ./server [port]        (default port 5050)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define MAX_CLIENTS      10
#define MAX_EQUIPMENT    5
#define BUF_SIZE         512
#define MAX_USERID       32

/* Hard-coded list of valid users for this demo application. */
static const char *VALID_USERS[] = {
    "alice", "bob", "carol", "dave", "erin"
};
#define NUM_VALID_USERS (sizeof(VALID_USERS) / sizeof(VALID_USERS[0]))

/* Equipment state and reservation status protected by the server mutex. */
typedef struct {
    char name[32];
    int  reserved;             /* 0 = available, 1 = reserved      */
    char reserved_by[MAX_USERID + 1];
} equipment_t;

static equipment_t equipment[MAX_EQUIPMENT] = {
    {"Oscilloscope",     0, ""},
    {"3D Printer",       0, ""},
    {"Soldering Station",0, ""},
    {"Logic Analyzer",   0, ""},
    {"Spectrometer",     0, ""}
};

static int connected_clients = 0;   /* count of currently connected sockets */
static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;

static int is_valid_user(const char *user_id) {
    for (size_t i = 0; i < NUM_VALID_USERS; i++) {
        if (strcmp(VALID_USERS[i], user_id) == 0) return 1;
    }
    return 0;
}

/* Print the current connection count and equipment reservation state.
 * This function is called only while the mutex is already held. */
static void print_status_locked(void) {
    printf("---- Server status ---- (connected clients: %d)\n",
           connected_clients);
    for (int i = 0; i < MAX_EQUIPMENT; i++) {
        printf("  %-18s : %s\n", equipment[i].name,
               equipment[i].reserved
                   ? equipment[i].reserved_by
                   : "available");
    }
    printf("------------------------\n");
    fflush(stdout);
}

/* ---------------------------------------------------------------
 * Per-client thread.
 * ------------------------------------------------------------- */
typedef struct {
    int sockfd;
    struct sockaddr_in addr;
} client_ctx_t;

static void *handle_client(void *arg) {
    client_ctx_t *ctx = (client_ctx_t *)arg;
    int sock = ctx->sockfd;
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &ctx->addr.sin_addr, client_ip, sizeof(client_ip));

    pthread_mutex_lock(&state_lock);
    connected_clients++;
    printf("[+] Client connected from %s (total connected: %d)\n",
           client_ip, connected_clients);
    pthread_mutex_unlock(&state_lock);

    char buf[BUF_SIZE];
    char user_id[MAX_USERID] = "";
    int authenticated = 0;

    while (1) {
        ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            /* Client disconnected unexpectedly, or a read error occurred.
             * Either way we clean up gracefully instead of crashing. */
            printf("[-] Client %s (%s) disconnected unexpectedly.\n",
                   client_ip, authenticated ? user_id : "unauthenticated");
            break;
        }
        buf[n] = '\0';
        /* strip trailing newline(s) */
        buf[strcspn(buf, "\r\n")] = '\0';

        if (strncmp(buf, "AUTH ", 5) == 0) {
            strncpy(user_id, buf + 5, MAX_USERID - 1);
            user_id[MAX_USERID - 1] = '\0';

            if (is_valid_user(user_id)) {
                authenticated = 1;
                printf("[AUTH] %s authenticated successfully.\n", user_id);

                char reply[BUF_SIZE];
                int off = snprintf(reply, sizeof(reply), "AUTH_OK\n");
                pthread_mutex_lock(&state_lock);
                for (int i = 0; i < MAX_EQUIPMENT; i++) {
                    off += snprintf(reply + off, sizeof(reply) - off,
                        "%s - %s\n", equipment[i].name,
                        equipment[i].reserved ? "RESERVED" : "AVAILABLE");
                }
                pthread_mutex_unlock(&state_lock);
                snprintf(reply + off, sizeof(reply) - off, "END\n");
                send(sock, reply, strlen(reply), 0);
            } else {
                authenticated = 0;
                printf("[AUTH] Rejected unknown user id '%s'.\n", buf + 5);
                send(sock, "AUTH_FAIL\n", 10, 0);
            }

        } else if (strncmp(buf, "RESERVE ", 8) == 0) {
            if (!authenticated) {
                /* Prevent unauthorized users from accessing equipment. */
                send(sock, "RESERVE_FAIL - not authenticated\n", 34, 0);
                continue;
            }
            char item[64];
            strncpy(item, buf + 8, sizeof(item) - 1);
            item[sizeof(item) - 1] = '\0';
            char reply[BUF_SIZE];

            pthread_mutex_lock(&state_lock); /* protects the reservation table */
            int found = 0;
            for (int i = 0; i < MAX_EQUIPMENT; i++) {
                if (strcmp(equipment[i].name, item) == 0) {
                    found = 1;
                    if (equipment[i].reserved) {
                        snprintf(reply, sizeof(reply),
                            "RESERVE_FAIL %s already reserved by %s\n",
                            item, equipment[i].reserved_by);
                    } else {
                        equipment[i].reserved = 1;
                        snprintf(equipment[i].reserved_by,
                                 sizeof(equipment[i].reserved_by),
                                 "%s", user_id);
                        snprintf(reply, sizeof(reply),
                            "RESERVE_OK %s\n", item);
                        printf("[RESERVE] %s reserved '%s'.\n", user_id, item);
                    }
                    break;
                }
            }
            if (!found) {
                snprintf(reply, sizeof(reply),
                    "RESERVE_FAIL %s unknown equipment\n", item);
            }
            print_status_locked();
            pthread_mutex_unlock(&state_lock);

            send(sock, reply, strlen(reply), 0);

        } else if (strncmp(buf, "QUIT", 4) == 0) {
            char reply[BUF_SIZE];
            snprintf(reply, sizeof(reply), "BYE %s\n",
                     authenticated ? user_id : "guest");
            send(sock, reply, strlen(reply), 0);
            printf("[QUIT] Session closed for %s.\n",
                   authenticated ? user_id : "guest");
            break;

        } else {
            send(sock, "ERROR unknown command\n", 23, 0);
        }
    }

    close(sock);
    pthread_mutex_lock(&state_lock);
    connected_clients--;
    pthread_mutex_unlock(&state_lock);
    free(ctx);
    return NULL;
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0); /* unbuffered so logs appear
                                          immediately even when redirected
                                          to a file/pipe for the demo */
    int port = 5050;
    if (argc > 1) port = atoi(argv[1]);

    /* Ignore SIGPIPE so that writing to a socket a client already closed
     * doesn't crash the whole server -- send() will just return -1
     * instead, which we already handle. */
    signal(SIGPIPE, SIG_IGN);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, MAX_CLIENTS) < 0) {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    printf("Lab equipment booking server listening on port %d ...\n", port);
    printf("Valid users: alice, bob, carol, dave, erin\n\n");

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd,
                                (struct sockaddr *)&client_addr,
                                &client_len);
        if (client_fd < 0) {
            perror("accept");
            continue; /* don't let one bad accept() bring the server down */
        }

        client_ctx_t *ctx = malloc(sizeof(client_ctx_t));
        ctx->sockfd = client_fd;
        ctx->addr = client_addr;

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, ctx) != 0) {
            perror("pthread_create");
            close(client_fd);
            free(ctx);
            continue;
        }
        pthread_detach(tid); /* auto-cleanup when the thread finishes */
    }

    close(listen_fd);
    return 0;
}
