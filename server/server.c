#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> //close
#include <string.h>

#include <arpa/inet.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define PORT "8080"
#define BACKLOG 10

/**
 * Helper just used to print the list of addressed. Not completely relevant for the example
 * @param list
 */
void pretty_inet_addr_list(struct addrinfo *list) {
    printf("Available addresses -----------------------\n");
    struct addrinfo *tmp = NULL;

    char s[INET6_ADDRSTRLEN] = "";

    for (tmp = list; tmp != NULL; tmp = tmp->ai_next) {
        switch (tmp->ai_family) {
            case AF_INET: {
                struct sockaddr_in *in = (struct sockaddr_in*)tmp->ai_addr;
                inet_ntop(AF_INET, &in, s, INET_ADDRSTRLEN);
                break;
            }
            case AF_INET6: {
                struct sockaddr_in6 *in = (struct sockaddr_in6*)tmp->ai_addr;
                inet_ntop(AF_INET6, &in, s, INET6_ADDRSTRLEN);
                break;
            }
            default:
                printf("%d %d %d\n", AF_INET, AF_INET6, tmp->ai_family);
                continue;
        }
        printf("%s\n", s);
    }
}

int main() {

    int rv = 0;

    /**
     * Get a list of addresses that we can bind to.
     * hints will limit the list that will be returned. if hints is null, all addresses will return
     * take care to zero out hints, or you can expect odd address selection behavior
     */
    struct addrinfo hints = {};
    struct addrinfo *bind_addr_list;
    //memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    rv = getaddrinfo(NULL, PORT, &hints, &bind_addr_list);
    if (rv != 0) {
        perror("problem looking up addresses");
        exit(1);
    }
    pretty_inet_addr_list(bind_addr_list);

    /**
     * now to get a socket from the system
     * first we grab a socket
     * next we set it to reusable
     * then we bind the internet address in our bind_addr_list to the socket
     *
     * since our address list has all of our information for everything we need to do, and the
     * sample doesn't care about the address, we'll just use the first one
     */
    int yes = 1;
    int listen_fd = 0;
    struct addrinfo *tmp;
    for (tmp = bind_addr_list; tmp != NULL; tmp = tmp->ai_next) {

        if ((listen_fd = socket(tmp->ai_family, tmp->ai_socktype, tmp->ai_protocol)) == -1) {
            perror("failed to get socket");
            continue;
        }

        if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("failed to set socket reusable");
            exit(1);
        }

        if (bind(listen_fd, tmp->ai_addr, tmp->ai_addrlen) == -1) {
            close(listen_fd);
            perror("Failed to find address to socket");
            continue;
        }

        break;
    }
    if (tmp == NULL) {
        perror("failed to bind any address to socket");

        if (listen_fd != -1) {
            close(listen_fd);
        }
        freeaddrinfo(bind_addr_list);
        exit(1);
    }
    freeaddrinfo(bind_addr_list);

    /**
     * and now we begin to listen
     */

    if (listen(listen_fd, BACKLOG) == -1) {
        perror("failed to begin listening");
        close(listen_fd);
        exit(1);
    }


    /**
     * when a connection comes in, it'll be given a new socket to talk over.
     * accept() blocks processing on listen_fd. When a new connection comes in, accept will create
     * a new socket for us and return it.
     *
     * if this is a server, accept will generally be wrapped up in an infinite loop.
     * Short lived connections may also be in the loop in a single threaded server (eg http)
     * Long lived would be maintained somewhere outside the loop (eg, game server, wek-sockets)
     */
    struct sockaddr_storage client_addr;
    socklen_t client_addr_len = sizeof(struct sockaddr_storage);
    int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_addr_len);
    if (client_fd == -1) {
        perror("Error while accepting the connection");
        close(listen_fd);
        exit(1);
    }

    /**
     * at this point, the server is all set up and waiting a connection. It'll block on the
     * accept() line above until a connection comes in. nothing below will occur until that happens.
     */

    char client_presentation[INET6_ADDRSTRLEN];
    if (client_addr.ss_family == AF_INET) {
        inet_ntop(client_addr.ss_family, (struct sockaddr_in*)&client_addr, client_presentation, INET6_ADDRSTRLEN);
    } else {        // assume ipv6
        inet_ntop(client_addr.ss_family, (struct sockaddr_in6*)&client_addr, client_presentation, INET6_ADDRSTRLEN);
    }
    printf("Got a connection from %s\n", client_presentation);


    /**
     * This is an echo server, so now we will read what the client is sending and then send it back
     * many samples show using read and write, but this sample will use recv and send
     *
     * read is generally equivalent to recv with 0 passed for flags, but recv only works on sockets
     * while read is a more general function that works on all descriptors.
     *
     * note when reading structs from a socket, we cannot simply put a pointer to the beginning
     * of a struct and fill it. We will need to translate each member accordingly from a buffer
     * using the ntoh* (network to host) functions. man ntohs has a list.
     *
     * our message will have a \n indicating the end of message.
     * \n is a generally poor choice. other non-display control characters better for this purpose
     *      ASCII has a named list of control characters (STX, ETX, etc)
     *      unicode gives U+0000-U+001F, U+007F, U+0080-U+009F and are unnamed
     *
     * some methods for detecting end of message (sockets api wont help, so here's some):
     *      -send length of message right at the beginning
     *      -detect an end of message character/sequence
     *      -protocol specifies message types with fixed lengths
     * clunkier
     *      -client closes connection
     *      -send plain text header that has a defined end sequence, but contains a property "CONTENT-LENGTH" if
     *          the message has more bytes in a body following the header (silly http...)
     */
    size_t max_len = 4096;
    char request_buf[max_len];
    char *request_cursor = &(request_buf[0]);
    memset(request_cursor, 0, max_len);
    size_t message_len = 0;
    while(1) {
        ssize_t num_bytes_read = recv(client_fd, request_cursor, max_len - message_len, 0);

        if (num_bytes_read > 0) {
            printf("Read bytes: %zd: %s\n", num_bytes_read, request_buf);
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

    /**
     * now we'll write the message back
     *
     * note when writing structs to a socket, we should not simply put a pointer to the beginning
     * of a struct and send it.
     *  1) We should translate to network byte order with hton* (man htons for a list)
     *  2) one should not alias a byte array with a struct as one cannot guarantee alignment between platforms
     */
    request_cursor = &(request_buf[0]);
    size_t still_to_write = message_len;
    while (1) {
        ssize_t num_bytes_sent = send(client_fd, request_cursor, still_to_write, 0);

        if (num_bytes_sent > 0) {
            printf("Bytes sent: %d\n", num_bytes_sent);

            still_to_write -= num_bytes_sent;
            if (still_to_write != 0) {
                request_cursor += num_bytes_sent;
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
     * and then do the cleanup
     */
    close(client_fd);
    close(listen_fd);

    return 0;
}
