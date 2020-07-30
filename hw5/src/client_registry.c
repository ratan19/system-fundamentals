#include "client_registry.h"
#include "csapp.h"
#include "debug.h"

//NOTE DO NOT RETURN BEFORE RELEASING LOCK
typedef struct fdlist_t {
	int fd;
	struct fdlist_t *next;
	struct fdlist_t *prev;
} fdlist_t;

/*
 * A client registry keeps track of the file descriptors for clients
 * that are currently connected.  Each time a client connects,
 * its file descriptor is added to the registry.  When the thread servicing
 * a client is about to terminate, it removes the file descriptor from
 * the registry.  The client registry also provides a function for shutting
 * down all client connections and a function that can be called by a thread
 * that wishes to wait for the client count to drop to zero.  Such a function
 * is useful, for example, in order to achieve clean termination:
 * when termination is desired, the "main" thread will shut down all client
 * connections and then wait for the set of registered file descriptors to
 * become empty before exiting the program.
 */
typedef struct client_registry {
	struct fdlist_t *fdlist;
	int fd_count;
	sem_t binsem;
} CLIENT_REGISTRY;


CLIENT_REGISTRY *creg_init(){
	void *t= Malloc(sizeof(CLIENT_REGISTRY));
	if(t == NULL){
		return NULL;
	}
	CLIENT_REGISTRY *cr = (CLIENT_REGISTRY *) t;
	cr->fd_count = 0;

	t = Malloc(sizeof(fdlist_t));
	if(t == NULL){
		return NULL;
	}
	cr->fdlist = (fdlist_t *) t;
	cr->fdlist->next = cr->fdlist;
	cr->fdlist->prev = cr->fdlist;
	sem_init(&cr->binsem, 0, 1);
	return cr;
}

/*
 * Finalize a client registry, freeing all associated resources.
 *
 * @param cr  The client registry to be finalized, which must not
 * be referenced again.
 */
void creg_fini(CLIENT_REGISTRY *cr){
	sem_destroy(&(cr->binsem));
	fdlist_t * tmp= NULL;
	fdlist_t * cur = cr->fdlist->next;
	while(cur != cr->fdlist){
		tmp = cur->next;
		Free(cur);
		cur=tmp;
	}
	Free(cr->fdlist);
	debug("succes fully finished client_registry");
}

/*
 * Register a client file descriptor.
 *
 * @param cr  The client registry.
 * @param fd  The file descriptor to be registered.
 * @return 0 if registration succeeds, otherwise -1.
 */
int creg_register(CLIENT_REGISTRY *cr, int fd){
	void *t= Malloc(sizeof(fdlist_t));
	if(t == NULL){
		return -1;
	}

	P(&cr->binsem);
	fdlist_t *fde = (fdlist_t *) t;
	fde->fd=fd;
	cr->fd_count++;
	fdlist_t *fdl = cr->fdlist;
	fdlist_t * temp  = fdl->next;
	fdl->next = fde;
	fde->prev = fdl;
	fde->next = temp;
	temp->prev = fde;
	V(&cr->binsem);
	return 0;
}

/*
 * Unregister a client file descriptor, alerting anybody waiting
 * for the registered set to become empty.
 *
 * @param cr  The client registry.
 * @param fd  The file descriptor to be unregistered.
 * @return 0  if unregistration succeeds, otherwise -1.
 */
int creg_unregister(CLIENT_REGISTRY *cr, int fd){
	debug("unregistering fd: %d",fd);
    P(&cr->binsem);
	fdlist_t * cur = cr->fdlist->next;
	int f = 0;
	while(cur != cr->fdlist){
		if(cur->fd == fd){
			debug("fd to be removed found");
			f = 1;
			break;
		}
		cur=cur->next;
	}
	if(f == 0){
		debug("fd to be removed NOT found");
		V(&cr->binsem);
		return -1;
	}
	cur->prev->next = cur->next;
	cur->next->prev= cur->prev;
	cr->fd_count = cr->fd_count -1;
	if(cr->fd_count == 0){
		debug("=============NOW ZERO=============");
	}
	debug("client count updated. fd_count: %d",cr->fd_count);
	V(&cr->binsem);
	return 0;
}

/*
 * A thread calling this function will block in the call until
 * the number of registered clients has reached zero, at which
 * point the function will return.
 *
 * @param cr  The client registry.
 */
void creg_wait_for_empty(CLIENT_REGISTRY *cr){


    time_t s = time(NULL);
    while (cr->fd_count != 0) {
    	debug("not freeing");
        sleep(1);
        time_t e = time(NULL);
        if(difftime(e,s) > 4){
            break;
        }
    }
    debug("fd count zero. exixiting from func.");
}

/*
 * Shut down all the currently registered client file descriptors.
 *
 * @param cr  The client registry.
 */
void creg_shutdown_all(CLIENT_REGISTRY *cr){
	debug("shutting down all");
	// P(&cr->binsem);
	fdlist_t * fdlist = cr->fdlist;
	fdlist_t * cur = fdlist->next;
	while(cur != fdlist){
		debug("shutting down fd : %d",cur->fd);
		int r = shutdown(cur->fd, SHUT_RD);
		cr->fd_count = cr->fd_count -1;
		if(r < 0){
			debug("error in shutting down");
		}
		cur = cur->next;
	}
    // V(&cr->binsem);
}

