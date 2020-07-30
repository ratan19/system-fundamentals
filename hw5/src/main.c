#include <stdlib.h>
#include "client_registry.h"
#include "exchange.h"
#include "trader.h"
#include "debug.h"
#include "csapp.h"
#include "server.h"
#include <signal.h>

extern EXCHANGE *exchange;
extern CLIENT_REGISTRY *client_registry;

static void terminate(int status);

/*
 * "Bourse" exchange server.
 *
 * Usage: bourse <port>
 */

void sighup_handler(int s){
    int prev_errno = errno;
    debug("sighup handler called");
    terminate(EXIT_SUCCESS);
    errno = prev_errno;
}

int main(int argc, char* argv[]){
    int listenfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    if (argc != 3) {
        fprintf(stderr, "usage: %s -p <port>\n", argv[0]);
        exit(0);
    }
    listenfd = Open_listenfd(argv[2]);
    client_registry = creg_init();
    exchange = exchange_init();
    trader_init();

//TODO
    Signal(SIGHUP, sighup_handler);
    pthread_t tid;


    // Perform required initializations of the client_registry,
    // maze, and player modules.


    // debug("Exchange in main: %p", exchange);
    // TODO: Set up the server socket and enter a loop to accept connections
    // on this socket.  For each connection, a thread should be started to
    // run function brs_client_service().  In addition, you should install
    // a SIGHUP handler, so that receipt of SIGHUP will perform a clean
    // shutdown of the server.

    while (1) {
        //check this
        int *connfdp;
        connfdp = Malloc(sizeof(int));
        clientlen = sizeof(struct sockaddr_storage);
        *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        // Getnameinfo((SA *) &clientaddr, clientlen, client_hostname, MAXLINE,client_port, MAXLINE, 0);
        // printf("Connected to (%s, %s)\n", client_hostname, client_port);

        if(*connfdp < 0){
            debug("error in accepting client connection");
            Free(connfdp);
            continue;
        }
        Pthread_create(&tid, NULL, brs_client_service, connfdp);
    }

    debug("You have to finish implementing main() "
	    "before the Bourse server will function.\n");

    terminate(EXIT_FAILURE);
}

/*
 * Function called to cleanly shut down the server.
 */
static void terminate(int status) {
    // Shutdown all client connections.
    // This will trigger the eventual termination of service threads.
    creg_shutdown_all(client_registry);

    debug("Waiting for service threads to terminate...");
    creg_wait_for_empty(client_registry);
    debug("All service threads terminated.");

    // Finalize modules.
    creg_fini(client_registry);
    exchange_fini(exchange);
    trader_fini();

    debug("Bourse server terminating");
    exit(status);
}



