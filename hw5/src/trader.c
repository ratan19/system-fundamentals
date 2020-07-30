#include "trader.h"
#include <string.h>
#include <pthread.h>
#include "csapp.h"
#include "debug.h"

TRADER * traders[MAX_TRADERS] = {0};

typedef struct trader {
	funds_t balance;
	quantity_t inventory;
	int fd;
	char *name;
	pthread_mutex_t tdmutex;
	int ref_count;
}TRADER;

typedef struct trader_map{
	sem_t tm_sem;
}trader_map;

static trader_map * tm;

int trader_init(){
	tm = (trader_map *) Malloc(sizeof(trader_map));
	if(tm == NULL){
		return -1;
	}
	sem_init(&(tm->tm_sem), 0, 1);
	pthread_mutexattr_t Attr;
	pthread_mutexattr_init(&Attr);
	pthread_mutexattr_settype(&Attr, PTHREAD_MUTEX_RECURSIVE);

	for(int i=0;i<MAX_TRADERS;i++){
		traders[i] =  (TRADER *)Malloc(sizeof(TRADER));
		traders[i]->balance = 0;
		traders[i]->inventory = 0;
		traders[i]->fd = -1;
		traders[i]->ref_count = 0;
		traders[i]->name=NULL;
		pthread_mutex_init(&(traders[i]->tdmutex), &Attr);
	}
	debug("trader module initialized");
	return 0;
}

void trader_fini(){
	sem_destroy(&(tm->tm_sem));
	for(int i=0;i<MAX_TRADERS;i++){
		debug("fini called for trader");
		trader_unref(traders[i],"terminate called");
		pthread_mutex_destroy(&(traders[i]->tdmutex));
		Free(traders[i]->name);
		Free(traders[i]);
	}
	Free(tm);
	debug("succes fully finished trader");
	return;
}

TRADER *trader_login(int fd, char *name){
	debug("inside login");
	if(tm == NULL){
		error("trader mp is null");
	}
	P(&(tm->tm_sem));
	debug("inside lock");
	int found = -1;
	for(int i=0;i<MAX_TRADERS;i++){
		if(traders[i]->fd < 0){
			if(found == -1){
				found = i;
			}
		}
		if(traders[i]->name != NULL){
			if(strcmp(name, traders[i]->name) == 0){
				debug("same name found");
				if(traders[i]->fd < 0){
					debug("valid returning user with same name");
					traders[i]->fd = fd;
					V(&(tm->tm_sem));
					return traders[i];
				}else{
					debug("invlaid user with same name");
					V(&(tm->tm_sem));
					return NULL;
				}
			}
		}
	}
	if(found == -1){
		V(&(tm->tm_sem));
		return NULL;
	}
	int l = strlen(name);
	traders[found]->name = Malloc(l+1);
	strcpy(traders[found]->name,name);
	traders[found]->fd = fd;
	trader_ref(traders[found], "trader maps ref count on login");
	//mutex
	V(&(tm->tm_sem));
	// debug("tm_sem released");
	return traders[found];
}

void trader_logout(TRADER *trader){
	debug("logging out trader");
	trader->fd=-1;
	trader_unref(trader,"ref decrease because of logout");
}

TRADER *trader_ref(TRADER *trader, char *why){
	if(trader == NULL){
		debug("null trader ref count inc");
		return NULL;
	}
	pthread_mutex_lock(&(trader->tdmutex));
	// debug("td object lock acquired for fd , %d", trader->fd);
	int k = trader->ref_count;
	trader->ref_count = k+ 1;
	debug("ref count increased from (%d -> %d) reason : %s",k, trader->ref_count  ,why);
	pthread_mutex_unlock(&(trader->tdmutex));
	// debug("td object lock released for fd , %d", trader->fd);
	return trader;
}

void trader_unref(TRADER *trader, char *why){
	if(trader == NULL){
		debug("null trader ref count dec");
		return;
	}
	pthread_mutex_lock(&(trader->tdmutex));
	// debug("td object lock acquired for fd , %d", trader->fd);
	int k = trader->ref_count;
	trader->ref_count = k - 1;
	debug("ref count decreased from (%d -> %d) reason : %s",k, trader->ref_count , why);
	pthread_mutex_unlock(&(trader->tdmutex));
	// debug("td object lock released for fd , %d", trader->fd);

	if(trader->ref_count == 0){
		debug("freeing trader: %s", trader->name);
		// trader = NULL;
		// Free(trader);
	}
	return;
}

int trader_send_packet(TRADER *trader, BRS_PACKET_HEADER *pkt, void *data){
	pthread_mutex_lock(&(trader->tdmutex));
	// debug("td object lock acquired for fd , %d", trader->fd);
	struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    pkt->timestamp_sec = htonl(time.tv_sec);
    pkt->timestamp_nsec = htonl(time.tv_nsec);
	int rs = proto_send_packet(trader->fd, pkt ,data);
	// Free(data);
	if(rs < 0){
		debug("trader send packet failed for fd : %d",trader->fd);
	}
	// debug("td object lock released for fd , %d", trader->fd);
	pthread_mutex_unlock(&(trader->tdmutex));
	return rs;
}

int trader_broadcast_packet(BRS_PACKET_HEADER *pkt, void *data){
	for(int i=0; i< MAX_TRADERS; i++){
		if(traders[i]->name != NULL){
			int rs = proto_send_packet(traders[i]->fd, pkt, data);
			if(rs == -1){
				debug("error in sending through broadcast");
				// Free(data);
				return -1;
			}
		}
	}
	// Free(data);
	return 0;
}

int trader_send_ack(TRADER *trader, BRS_STATUS_INFO *info){
	pthread_mutex_lock(&(trader->tdmutex));
	// debug("td object lock acquired for fd , %d", trader->fd);
	BRS_PACKET_HEADER *hdr = (BRS_PACKET_HEADER *) Malloc(sizeof(BRS_PACKET_HEADER));
	hdr->type = BRS_ACK_PKT;
	hdr->size = htons(sizeof(BRS_STATUS_INFO));
	struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    hdr->timestamp_sec = htonl(time.tv_sec);
    hdr->timestamp_nsec = htonl(time.tv_nsec);
	info->balance = htonl(trader->balance);
	info->inventory = htonl(trader->inventory);
	int rs = trader_send_packet(trader, hdr, info);
	if(rs < 0){
		debug("error in sending from trader send ack");
	}
	pthread_mutex_unlock(&(trader->tdmutex));
	// debug("td object lock released for fd , %d", trader->fd);
	// Free(hdr);
	// Free(info);
	return rs;
}

int trader_send_nack(TRADER *trader){
	BRS_PACKET_HEADER *hdr = (BRS_PACKET_HEADER *) Malloc(sizeof(BRS_PACKET_HEADER));
	hdr->type = BRS_NACK_PKT;
	hdr->size = 0;//htons(sizeof(BRS_STATUS_INFO));
	int rs = trader_send_packet(trader, hdr ,NULL);
	if(rs == -1){
		debug("error in sending nack in trader module");
	}
	// pthread_mutex_unlock(&(trader->tdmutex));
	// debug("td object lock released for fd , %d", trader->fd);
	// Free(hdr);
	return rs;
}

/*
 * Increase the balance for a trader.
 *
 * @param trader  The trader whose balance is to be increased.
 * @param amount  The amount by which the balance is to be increased.
 */
void trader_increase_balance(TRADER *trader, funds_t amount){

	pthread_mutex_lock(&(trader->tdmutex));
	// debug("td object lock acquired for fd , %d", trader->fd);

	trader->balance = trader->balance + amount;
	debug("trader [%s] balance(inc) updated to : %d ",trader->name, trader->balance);

	pthread_mutex_unlock(&(trader->tdmutex));
	// debug("td object lock released for fd , %d", trader->fd);

	return;
}

/*
 * Attempt to decrease the balance for a trader.
 *
 * @param trader  The trader whose balance is to be decreased.
 * @param amount  The amount by which the balance is to be decreased.
 * @return 0 if the original balance is at least as great as the
 * amount of decrease, -1 otherwise.
 */
int trader_decrease_balance(TRADER *trader, funds_t amount){
	int rs=0;
	pthread_mutex_lock(&(trader->tdmutex));
	// debug("td object lock acquired for fd , %d", trader->fd);

	if(trader->balance < amount){
		rs = -1;
		debug("trader [%s] balance could not be updated. Less balance: ",trader->name);
	}else{
		trader->balance = trader->balance - amount;
		debug("trader [%s] balance(dec) updated to : %d ",trader->name, trader->balance);
	}

	pthread_mutex_unlock(&(trader->tdmutex));
	// debug("td object lock released for fd , %d", trader->fd);
	return rs;
}

/*
 * Increase the inventory for a trader by a specified quantity.
 *
 * @param trader  The trader whose inventory is to be increased.
 * @param amount  The amount by which the inventory is to be increased.
 */
void trader_increase_inventory(TRADER *trader, quantity_t quantity){
	pthread_mutex_lock(&(trader->tdmutex));
	// debug("td object lock acquired for fd , %d", trader->fd);

	trader->inventory = trader->inventory + quantity;
	debug("trader [%s] inventory(inc) updated to : %d ",trader->name, trader->inventory);

	pthread_mutex_unlock(&(trader->tdmutex));
	// debug("td object lock released for fd , %d", trader->fd);
	return;
}

/*
 * Attempt to decrease the inventory for a trader by a specified quantity.
 *
 * @param trader  The trader whose inventory is to be decreased.
 * @param amount  The amount by which the inventory is to be decreased.
 * @return 0 if the original inventory is at least as great as the
 * amount of decrease, -1 otherwise.
 */
int trader_decrease_inventory(TRADER *trader, quantity_t quantity){
	int rs=0;
	pthread_mutex_lock(&(trader->tdmutex));
	// debug("td object lock acquired for fd , %d", trader->fd);

	if(trader->inventory < quantity){
		rs = -1;
		debug("trader [%s] inventory could not be updated. Less Inventory: ",trader->name);
	}else{
		trader->inventory = trader->inventory - quantity;
		debug("trader [%s] inventory(inc) updated to : %d ",trader->name, trader->inventory);
	}

	pthread_mutex_unlock(&(trader->tdmutex));
	// debug("td object lock released for fd , %d", trader->fd);
	return rs;
}