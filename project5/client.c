/*
 * client.c
 *
 * TCP client for the university lab equipment booking system.
 * Connects to the server, authenticates with a user ID, displays the
 * equipment list, lets the user reserve one item, then closes the
 * session gracefully.
 *
 * Build: gcc -Wall -O2 -o client client.c
 * Run:   ./client <server_ip> <port> <user_id> [equipment_name]
 *
 *   If [equipment_name] is supplied, the client runs non-interactively
 *   (useful for scripted demos / automated testing of concurrent
 *   reservation attempts). Otherwise it prompts interactively.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define BUF_SIZE 1024

static ssize_t recv_line_block(int sock, char *buf, size_t maxlen) {
    /* Read the server response into a buffer for the simple line-based protocol. */
    ssize_t n = recv(sock, buf, maxlen - 1, 0);
    if (n > 0) buf[n] = '\0';
    return n;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr,
            "Usage: %s <server_ip> <port> <user_id> [equipment_name]\n",
            argv[0]);
        return 1;
    }
    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    const char *user_id = argv[3];
    const char *auto_equipment = (argc > 4) ? argv[4] : NULL;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid server IP address: %s\n", server_ip);
        close(sock);
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0) {
        perror("connect");
        close(sock);
        return 1;
    }

    char buf[BUF_SIZE];

    /* Send the authentication request and wait for the server reply. */
    snprintf(buf, sizeof(buf), "AUTH %s\n", user_id);
    send(sock, buf, strlen(buf), 0);

    ssize_t n = recv_line_block(sock, buf, sizeof(buf));
    if (n <= 0) {
        fprintf(stderr, "Server closed the connection unexpectedly.\n");
        close(sock);
        return 1;
    }

    if (strncmp(buf, "AUTH_FAIL", 9) == 0) {
        printf("Authentication FAILED for user '%s'.\n", user_id);
        close(sock);
        return 1;
    }

    printf("Authentication SUCCESSFUL for user '%s'.\n", user_id);
    printf("Available equipment:\n%s", buf + 8 /* skip "AUTH_OK\n" */);

    /* Ask the user which equipment to reserve. */
    char chosen[64];
    if (auto_equipment) {
        strncpy(chosen, auto_equipment, sizeof(chosen) - 1);
        chosen[sizeof(chosen) - 1] = '\0';
        printf("\n(Auto mode) Requesting reservation of: %s\n", chosen);
    } else {
        printf("\nEnter the name of the equipment you'd like to reserve: ");
        fflush(stdout);
        if (fgets(chosen, sizeof(chosen), stdin) == NULL) {
            close(sock);
            return 1;
        }
        chosen[strcspn(chosen, "\n")] = '\0';
    }

    snprintf(buf, sizeof(buf), "RESERVE %s\n", chosen);
    send(sock, buf, strlen(buf), 0);

    n = recv_line_block(sock, buf, sizeof(buf));
    if (n > 0) {
        printf("Server response: %s", buf);
    }

    /* Close the session gracefully after the reservation attempt. */
    send(sock, "QUIT\n", 5, 0);
    n = recv_line_block(sock, buf, sizeof(buf));
    if (n > 0) {
        printf("%s", buf); /* e.g. "BYE alice" */
    }
    printf("Session closed. Goodbye, %s\n", user_id);

    close(sock);
    return 0;
}
