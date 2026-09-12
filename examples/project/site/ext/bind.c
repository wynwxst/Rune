#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUFFER_SIZE 8192

static const char *HTML =
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "  <title>C Socket Website</title>"
    "  <meta charset=\"UTF-8\">"
    "  <style>"
    "    body { font-family: sans-serif; text-align: center; margin-top: 15%%; }"
    "    h1 { color: #333; }"
    "  </style>"
    "</head>"
    "<body>"
    "  <h1>Hello from C!</h1>"
    "  <p>This website is being served directly from a C socket.</p>"
    "</body>"
    "</html>";

static void handle_client(int client_fd)
{
    char buffer[BUFFER_SIZE];

    ssize_t received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

    if (received <= 0) {
        close(client_fd);
        return;
    }

    buffer[received] = '\0';

    printf("Request:\n%s\n", buffer);

    /*
     * Only serve a response for HTTP requests.
     * This is deliberately simple and doesn't implement a full HTTP parser.
     */
    const char *body = HTML;

    char response[BUFFER_SIZE];

    int length = snprintf(
        response,
        sizeof(response),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        strlen(body),
        body
    );

    send(client_fd, response, length, 0);

    close(client_fd);
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <address> <port>\n", argv[0]);
        fprintf(stderr, "Example: %s 0.0.0.0 8080\n", argv[0]);
        return 1;
    }

    const char *address = argv[1];
    int port = atoi(argv[2]);

    if (port < 1 || port > 65535) {
        fprintf(stderr, "Invalid port: %d\n", port);
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    /*
     * Create TCP socket.
     */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    /*
     * Allow immediate reuse of the address after restarting.
     */
    int reuse = 1;

    if (setsockopt(
            server_fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &reuse,
            sizeof(reuse)) < 0) {
        perror("setsockopt");
        close(server_fd);
        return 1;
    }

    /*
     * Convert the supplied address to an IPv4 address.
     *
     * 0.0.0.0 means listen on every IPv4 interface.
     * 127.0.0.1 means localhost only.
     */
    struct sockaddr_in server_addr;

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, address, &server_addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid IPv4 address: %s\n", address);
        close(server_fd);
        return 1;
    }

    /*
     * Bind socket to address:port.
     */
    printf("%d|%lu\n",server_fd,sizeof(server_addr));
    if (bind(
            server_fd,
            (struct sockaddr *)&server_addr,
            sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    /*
     * Start listening.
     */
    if (listen(server_fd, 128) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("Server listening on %s:%d\n", address, port);

    /*
     * Accept clients forever.
     */
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(
            server_fd,
            (struct sockaddr *)&client_addr,
            &client_len
        );

        if (client_fd < 0) {
            if (errno == EINTR)
                continue;

            perror("accept");
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];

        inet_ntop(
            AF_INET,
            &client_addr.sin_addr,
            client_ip,
            sizeof(client_ip)
        );

        printf("Connection from %s:%d\n",
               client_ip,
               ntohs(client_addr.sin_port));

        handle_client(client_fd);
    }

    close(server_fd);
    return 0;
}