#include <stdio.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <string.h>

#define PORT "8080"

int main() {

    char *host = "localhost";

    int rv = 0;

    struct addrinfo hints = {};
    struct addrinfo *bind_addr_list;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    rv = getaddrinfo(host, PORT, &hints, &bind_addr_list);


    /**
     * now to connect, and again, I'll just connect to the first entry found
     */
    int server_fd = 0;
    struct addrinfo *tmp;
    for (tmp = bind_addr_list; tmp != NULL; tmp = tmp->ai_next) {

        server_fd = socket(tmp->ai_family, tmp->ai_socktype, tmp->ai_protocol);
        if (server_fd == -1) {
            perror("failed to create socket\n");
            continue;
        }

        if (connect(server_fd, tmp->ai_addr, tmp->ai_addrlen) == -1) {
            close(server_fd);
            perror("Failed to connect to server\n");
            continue;
        }
        break;
    }
    if (tmp == NULL) {
        close(server_fd);
        perror("failed to establish connection.\n");
        exit(1);
    }

    char server_presentation[INET6_ADDRSTRLEN];
    if (tmp->ai_family == AF_INET) {
        inet_ntop(tmp->ai_addr->sa_family, (struct sockaddr_in*)&tmp->ai_addr, server_presentation, INET6_ADDRSTRLEN);
    } else {        // assume ipv6
        inet_ntop(tmp->ai_addr->sa_family, (struct sockaddr_in6*)&tmp->ai_addr, server_presentation, INET6_ADDRSTRLEN);
    }
    printf("Client connecting to %s\n", server_presentation);

    freeaddrinfo(bind_addr_list);



    /**
     * now to write then read
     */
    char *message = "The message to be read: hello world!\n";
    char *message_cursor = message;
    size_t len = strlen(message);
    size_t still_to_write = len;
    while (1) {
        ssize_t num_bytes_sent = send(server_fd, message_cursor, still_to_write, 0);

        if (num_bytes_sent > 0) {
            printf("Bytes sent: %zd\n", num_bytes_sent);

            still_to_write -= num_bytes_sent;
            if (still_to_write != 0) {
                message_cursor += num_bytes_sent;
                continue;
            }

            break;
        } else if (num_bytes_sent == 0) {
            printf("No more to send\n");
            break;
        } else {
            perror("error writing to socket. check errno\n");
            break;
        }
    }



    /**
     * then read
     */
    size_t max_len = 4096;
    char request_buf[max_len];
    char *request_cursor = &(request_buf[0]);
    memset(request_cursor, 0, max_len);
    size_t message_len = 0;
    while(1) {
        ssize_t num_bytes_read = recv(server_fd, request_cursor, max_len - message_len, 0);

        if (num_bytes_read > 0) {
            printf("Read bytes: %zd : %s\n", num_bytes_read, request_buf);
            message_len += num_bytes_read;

            if (request_buf[message_len - 1] == '\n') {
                break;
            } else {
                request_cursor += message_len;
                continue;
            }

        } else if (num_bytes_read == 0) {
            printf("No more to read\n");
            break;
        } else {
            printf("error reading from socket. check errno\n");
            break;
        }
    }


    close(server_fd);

    return 0;
}
