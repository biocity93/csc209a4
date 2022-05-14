#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>    /* Internet domain header */

#include "wrapsock.h"
#include "ws_helpers.h"

#define MAXCLIENTS 10

int handleClient(struct clientstate *cs, char *line);
// You may want to use this function for initial testing
//void write_page(int fd);

int main(int argc, char **argv) {

    if(argc != 2) {
        fprintf(stderr, "Usage: wserver <port>\n");
        exit(1);
    }
    unsigned short port = (unsigned short)atoi(argv[1]);
    int listenfd;
    struct clientstate client[MAXCLIENTS];


    // Set up the socket to which the clients will connect
    listenfd = setupServerSocket(port);

    initClients(client, MAXCLIENTS);

    int max_fd = listenfd;
    fd_set all_fds;
    FD_ZERO(&all_fds);
    FD_SET(listenfd, &all_fds);
    int num_read[MAXCLIENTS];
    int num_read_new[MAXCLIENTS];
    int disconnect_client[MAXCLIENTS];
    struct timeval timeout;
    int sret; 

    //initialize num read
    for (int i = 0; i < MAXCLIENTS; i++) {
        num_read[i] = 0;
        disconnect_client[i] = 0;
    }        

    while (1) {
        // select updates the fd_set it receives, so we always use a copy and retain the original.
        fd_set listen_fds = all_fds;
        timeout.tv_sec = 10; 
        timeout.tv_usec = 0; 

        sret = select(max_fd + 1, &listen_fds, NULL, NULL, &timeout);

        if(sret == 0){
            printf("Con timeout\n");
            for (int index = 0; index < MAXCLIENTS; index++) {
                if (client[index].sock > -1 && FD_ISSET(client[index].sock, &listen_fds)) {
                    close(client[index].sock);
                    FD_CLR(client[index].sock, &all_fds);
                    resetClient(&client[index]);
                }
            }
            exit(0);
        }
        else{
            if (select(max_fd + 1, &listen_fds, NULL, NULL, NULL) == -1) {
                perror("server: select");
                exit(1);
            }

            // Is it the original socket? Create a new connection ...
            if (FD_ISSET(listenfd, &listen_fds)) {
                int user_index = 0;
                while (user_index < MAXCLIENTS && client[user_index].sock != -1) {
                    user_index++;
                }
                if (user_index == MAXCLIENTS) {
                    fprintf(stderr, "server: max concurrent connections\n");
                    resetClient(client);
                    return -1;
                }
                int client_fd = Accept(listenfd, NULL, NULL);
                if (client_fd > max_fd) {
                    max_fd = client_fd;
                }
                client[user_index].sock = client_fd;
                client[user_index].request = malloc(MAXLINE + 1);
                client[user_index].output = malloc(MAXPAGE);
                FD_SET(client_fd, &all_fds);
                printf("Accepted connection\n");
            }

            // Next, check the clients.
            for (int index = 0; index < MAXCLIENTS; index++) {
                if (client[index].sock > -1 && FD_ISSET(client[index].sock, &listen_fds)) {
                    num_read_new[index] = read(client[index].sock, &client[index].request[num_read[index]], MAXLINE);
                    if (num_read_new[index] == 0) {
                    disconnect_client[index] = 1;
                    }
                    num_read[index] = num_read[index]+num_read_new[index];
                    client[index].request[num_read[index]] = '\0';
                    int handle_client_message;
                    if (disconnect_client[index] == 0) {
                        handle_client_message = handleClient(&client[index], client[index].request);
                    }
                    if ((handle_client_message < 0) || (disconnect_client[index] == 1)) {
                        int saved_client_id = client[index].sock;
                        close(client[index].sock);
                        FD_CLR(client[index].sock, &all_fds);
                        resetClient(&client[index]);
                        num_read[index] = 0;
                        disconnect_client[index] = 0;
                        printf("Client %d disconnected\n", saved_client_id);
                    } 
                    else if(handle_client_message == 1){
                        processRequest(&client[index]);
                        close(client[index].fd[0]);
                        num_read[index] = 0;
                        printf("Echoing message from client %d\n", client[index].sock);
                        resetClientBuf(&client[index]);
                    }
                }
            }
        }
    }
    return 0; 
}

/* Update the client state cs with the request input in line.
 * Intializes cs->request if this is the first read call from the socket.
 * Note that line must be null-terminated string.
 *
 * Return 0 if the get request message is not complete and we need to wait for
 *     more data
 * Return -1 if there is an error and the socket should be closed
 *     - Request is not a GET request
 *     - The first line of the GET request is poorly formatted (getPath, getQuery)
 * 
 * Return 1 if the get request message is complete and ready for processing
 *     cs->request will hold the complete request
 *     cs->path will hold the executable path for the CGI program
 *     cs->query will hold the query string
 *     cs->output will be allocated to hold the output of the CGI program
 *     cs->optr will point to the beginning of cs->output
 */
int handleClient(struct clientstate *cs, char *line) {
    //cs->path = getPath(line);
    //cs->query_string = getQuery(line);
    if (strlen(line) < 4){ // no \r\n\r\n
        return 0;
    } else if ((line[strlen(line)-1] == '\n') && (line[strlen(line)-2] == '\r') && (line[strlen(line)-3] == '\n') && (line[strlen(line)-4] == '\r')) {
        cs->path = getPath(line);
        cs->query_string = getQuery(line);

        if (cs->path == NULL){ //invalid GET
            return -1;
        }
        else if (strcmp("favicon.ico", cs->path) == 0){ 
            // A suggestion for debugging output
            fprintf(stderr, "Client: sock = %d\n", cs->sock);
            fprintf(stderr, "        path = %s (ignoring)\n", cs->path);
            return -1;
        } 
        else{
		    return 1;
	    }
    } 
    else {
	    return 0;
    }
}
