#include "exchange.h"
#include "csapp.h"
#include "debug.h"
#include "protocol.h"

//NOTE DO NOT RETURN BEFORE RELEASING LOCK
typedef struct buy_order_t {
	orderid_t orderid;
	funds_t bid;
	quantity_t inventory;
	struct buy_order_t *next;
	struct buy_order_t *prev;
	TRADER *trader;
} BUY_ORDER_T;

typedef struct sell_order_t {
	orderid_t orderid;
	funds_t ask;
	quantity_t inventory;
	struct sell_order_t *next;
	struct sell_order_t *prev;
	TRADER *trader;
} SELL_ORDER_T;

typedef struct exchange {
	int order_count;
	funds_t last_trade_price;
	BUY_ORDER_T *buy_orders;
	SELL_ORDER_T *sell_orders;
}EXCHANGE;

void remove_b_order(BUY_ORDER_T *bo);
void remove_s_order(SELL_ORDER_T *so);
void addto_buyorder(BUY_ORDER_T *bo, EXCHANGE *xchg);
void addto_sellorder(SELL_ORDER_T *bo, EXCHANGE *xchg);
void *matchmaker(void *arg);
void settle_order(BUY_ORDER_T *bo, SELL_ORDER_T *so, EXCHANGE *xchg);
void send_info(quantity_t quantity,funds_t price );
void send_cancel_info(orderid_t buyer, orderid_t seller, quantity_t quantity);


pthread_mutex_t matchmakerlock;
pthread_cond_t lockcond;
int fin;

void *matchmaker(void *arg){
	EXCHANGE *xchg = (EXCHANGE *)arg;
	int this_order_count=xchg->order_count;
	debug("in matchmaker thread");

	pthread_mutex_lock(&matchmakerlock);

	while(1){
		while(this_order_count == xchg->order_count && fin == 0){
			pthread_cond_wait(&lockcond, &matchmakerlock);;
		}

		if(fin == 1){
			pthread_mutex_unlock(&matchmakerlock);
			pthread_mutex_destroy(&matchmakerlock);
			pthread_cond_destroy(&lockcond);
			debug("cleaned up mutexes");
			fin=0;
		}
		while(1){
			int changed=0;
			BUY_ORDER_T *bo = xchg->buy_orders;
			BUY_ORDER_T *bcur = bo->next;
			debug("continue");
			sleep(1);
			while(bcur != bo){
				debug("=======outer");
				SELL_ORDER_T *so = xchg->sell_orders;
				SELL_ORDER_T *scur = so->next;
				while(scur != so){
					debug("=======inner");
					if(scur->ask < bcur->bid){
						debug("=======condition met");
						changed=1;
						settle_order(bcur,scur,xchg);
					}
					scur = scur->next;
				}
				bcur = bcur->next;
			}
			if(changed == 0){
				debug("no more matches. breaking");
				break;
			}
		}
		this_order_count = xchg->order_count;

		pthread_mutex_unlock(&matchmakerlock);
	}
	return NULL;
}

void settle_order(BUY_ORDER_T *bo, SELL_ORDER_T *so, EXCHANGE *xchg){
	quantity_t q_tosold = so->inventory;
	quantity_t q_asked = bo->inventory;
	funds_t settled_price;

	if(so->ask < xchg->last_trade_price){
		debug("=============ask less than last value");
		settled_price = xchg->last_trade_price;
	}else{
		debug("=============ask greater than last value");
		settled_price = so->ask;
	}
	xchg->last_trade_price=settled_price;
	quantity_t settled_quantity;
	if(q_tosold < q_asked){
		settled_quantity = q_tosold;
	}else{
		settled_quantity = q_asked;
	}
	//update buyer
	TRADER *bt = bo->trader;
	bo->inventory = q_asked - settled_quantity;
	trader_increase_inventory(bt, settled_quantity); //cor
	debug("update buyers inventory: %d", bo->inventory);
	trader_increase_balance(bt, (bo->inventory*bo->bid) - (settled_quantity)*(settled_price) );
	TRADER *st = so->trader;
	so->inventory = q_tosold - settled_quantity;
	trader_increase_inventory(st, so->inventory);
	debug("update seller inventory: %d", so->inventory);
	trader_increase_balance(st, (settled_quantity)*(settled_price) ); //cor
	void * t = Malloc(sizeof(BRS_NOTIFY_INFO));
	if(t == NULL){
		return;
	}
	BRS_NOTIFY_INFO * notif = (BRS_NOTIFY_INFO *) t;
	notif->seller = htonl(so->orderid);
	notif->buyer = htonl(bo->orderid);
	notif->quantity = htonl(settled_quantity);
	notif->price = htonl(settled_price);
	BRS_PACKET_HEADER *pkt = (BRS_PACKET_HEADER *) Malloc(sizeof(BRS_PACKET_HEADER));
	if(pkt == NULL){
		return;
	}
	pkt->type=BRS_BOUGHT_PKT;
	int sz = htons(sizeof(BRS_NOTIFY_INFO));
	pkt->size=sz;
	trader_send_packet(bt, pkt, notif);
	pkt->type=BRS_SOLD_PKT;
	trader_send_packet(st, pkt, notif);
	pkt->type=BRS_TRADED_PKT;
	trader_broadcast_packet( pkt, notif);
	if(bo->inventory == 0){
		remove_b_order(bo);
	}
	if(so->inventory == 0){
		remove_s_order(so);
	}
	// sleep(1);
	Free(notif);
	Free(pkt);
}

/*
 * Initialize a new exchange.
 *
 * @return  the newly initialized exchange, or NULL if initialization failed.
 */
EXCHANGE *exchange_init(){
	fin=0;
	debug("initialization of exchange");
	void *t= Malloc(sizeof(EXCHANGE));
	if(t == NULL){
		return NULL;
	}
	EXCHANGE *ex = (EXCHANGE *) t;
	ex->order_count = 0;
	ex->last_trade_price= 0;
	t=NULL;
	t = Malloc(sizeof(BUY_ORDER_T));
	if(t ==NULL){
		return NULL;
	}
	ex->buy_orders = (BUY_ORDER_T *) t;
	ex->buy_orders->next = ex->buy_orders;
	ex->buy_orders->prev = ex->buy_orders;
	debug("buy order sentinal node: %p", ex->buy_orders);
	t=NULL;
	t = Malloc(sizeof(SELL_ORDER_T));
	if(t == NULL){
		return NULL;
	}
	ex->sell_orders = (SELL_ORDER_T *) t;
	ex->sell_orders->next = ex->sell_orders;
	ex->sell_orders->prev = ex->sell_orders;
	debug("sell order sentinal node: %p", ex->sell_orders);
	pthread_t tid;
	Pthread_create(&tid, NULL, &matchmaker, ex);
	return ex;
}

/*
 * Finalize an exchange, freeing all associated resources.
 *
 * @param xchg  The exchange to be finalized, which must not
 * be referenced again.
 */
void exchange_fini(EXCHANGE *xchg){
	BUY_ORDER_T *bo = xchg->buy_orders;
	BUY_ORDER_T *bcur = bo->next;
	while(bcur != bo){
		BUY_ORDER_T *temp = bcur->next;
		Free(bcur);
		bcur = temp;
	}
	Free(bo);
	SELL_ORDER_T *so = xchg->sell_orders;
	SELL_ORDER_T *scur = so->next;
	while(scur != so){
		SELL_ORDER_T *temp = scur->next;
		Free(scur);
		scur = temp;
	}
	Free(so);
	Free(xchg);

	fin=1;
	pthread_cond_signal(&lockcond);

	time_t s = time(NULL);
	while(fin != 0){
		sleep(0.5);
		time_t e = time(NULL);
        if(difftime(e,s) > 4){
            break;
        }
	}
	// pthread_mutex_destroy(&matchmakerlock);
	// debug("succes fully finished exchange");
	// pthread_cond_destroy(&lockcond);

}

funds_t get_bid(EXCHANGE *xchg){
	BUY_ORDER_T *bo = xchg->buy_orders;
	BUY_ORDER_T *bcur = bo->next;
	funds_t max = 0;
	while(bcur != bo){
		if(bcur->bid > max ){
			max = bcur->bid;
		}
		bcur = bcur->next;
	}
	return max;
}

funds_t get_ask(EXCHANGE *xchg){
	SELL_ORDER_T *so = xchg->sell_orders;
	SELL_ORDER_T *scur = so->next;
	funds_t max = 0;
	while(scur != so){
		if(scur->ask > max ){
			max = scur->ask;
		}
		scur = scur->next;
	}
	return max;
}

/*
 * Get the current status of the exchange.
 */
void exchange_get_status(EXCHANGE *xchg, BRS_STATUS_INFO *infop){
	infop->bid = htonl(get_bid(xchg));
	infop->ask = htonl(get_ask(xchg));
	infop->last=htonl(xchg->last_trade_price);
	return;
}

orderid_t exchange_post_buy(EXCHANGE *xchg, TRADER *trader, quantity_t quantity,funds_t price){
	void * t = Malloc(sizeof(BUY_ORDER_T));
	if(t == NULL){
		return 0;
	}
	pthread_mutex_lock(&matchmakerlock);
	xchg->order_count = xchg->order_count+1;
	BUY_ORDER_T * bo = (BUY_ORDER_T *) t;
	trader_ref(trader, "placing a order of type [buy]");
	bo->trader = trader;
	bo->bid = price;
	bo->inventory = quantity;
	bo->orderid = xchg->order_count;
	debug("adding buy object [%p] to buy order list.",bo);
	addto_buyorder(bo, xchg);
	debug("added successfully to buy order list");
	BRS_PACKET_HEADER * pkt = (BRS_PACKET_HEADER *) Malloc(sizeof(BRS_PACKET_HEADER));
	if(pkt ==NULL){
		pthread_mutex_unlock(&matchmakerlock);
		return 0;
	}
	pkt->size = htons(sizeof(BRS_NOTIFY_INFO));
	pkt->type = BRS_POSTED_PKT;
	BRS_NOTIFY_INFO * boi = (BRS_NOTIFY_INFO *) Malloc(sizeof(BRS_NOTIFY_INFO));
	if(boi == NULL){
		pthread_mutex_unlock(&matchmakerlock);
		return 0;
	}
	boi->quantity = htonl(quantity);
	boi->price= htonl(price);
	boi->buyer = htonl(bo->orderid);
	boi->seller = 0;
	trader_broadcast_packet(pkt,boi);
	Free(pkt);
	Free(boi);
	pthread_cond_signal(&lockcond);
	pthread_mutex_unlock(&matchmakerlock);
	return xchg->order_count;
}

/*
 * Post a sell order on the exchange on behalf of a trader.
 * The trader is stored with the order, and its reference count is
 * increased by one to account for the stored pointer.
 * Inventory equal to the amount of the order is
 * encumbered by removing it from the trader's account.
 * A POSTED packet containing details of the order is broadcast
 * to all logged-in traders.
 *
 * @param xchg  The exchange to which the order is to be posted.
 * @param trader  The trader on whose behalf the order is to be posted.
 * @param quantity  The quantity to be sold.
 * @param price  The minimum sale price per unit.
 * @return  The order ID assigned to the new order, if successfully posted,
 * otherwise 0.
 */
orderid_t exchange_post_sell(EXCHANGE *xchg, TRADER *trader, quantity_t quantity,funds_t price){
	void * t = Malloc(sizeof(BUY_ORDER_T));
	if(t == NULL){
		return 0;
	}
	pthread_mutex_lock(&matchmakerlock);\
	xchg->order_count = xchg->order_count+1;
	SELL_ORDER_T * so = (SELL_ORDER_T *) t;
	trader_ref(trader, "placing a order of type [sell]");
	so->trader = trader;
	so->ask = price;
	so->inventory = quantity;
	so->orderid = xchg->order_count;
	debug("adding sell object [%p] to sell order list. ",so);
	addto_sellorder(so, xchg);
	debug("added successfully to sell order list");
	BRS_PACKET_HEADER * pkt = (BRS_PACKET_HEADER *) Malloc(sizeof(BRS_PACKET_HEADER));
	if(pkt == NULL){
		pthread_mutex_unlock(&matchmakerlock);
		return 0;
	}
	pkt->size = htons(sizeof(BRS_NOTIFY_INFO));
	pkt->type = BRS_POSTED_PKT;
	BRS_NOTIFY_INFO * boi = (BRS_NOTIFY_INFO *) Malloc(sizeof(BRS_NOTIFY_INFO));
	if(boi == NULL){
		pthread_mutex_unlock(&matchmakerlock);
		return 0;
	}
	boi->quantity = htonl(quantity);
	boi->price= htonl(price);
	boi->seller = htonl(so->orderid);
	boi->buyer = 0;
	trader_broadcast_packet(pkt,boi);
	Free(pkt);
	Free(boi);
	pthread_cond_signal(&lockcond);
	pthread_mutex_unlock(&matchmakerlock);
	return xchg->order_count;
}

/*
 * Attempt to cancel a pending order.
 * If successful, the quantity of the canceled order is returned in a variable,
 * and a CANCELED packet containing details of the canceled order is
 * broadcast to all logged-in traders.
 *
 * @param xchg  The exchange from which the order is to be cancelled.
 * @param trader  The trader cancelling the order is to be posted,
 * which must be the same as the trader who originally posted the order.
 * @param id  The order ID of the order to be cancelled.
 * @param quantity  Pointer to a variable in which to return the quantity
 * of the order that was canceled.  Note that this need not be the same as
 * the original order amount, as the order could have been partially
 * fulfilled by trades.
 * @return  0 if the order was successfully cancelled, -1 otherwise.
 * Note that cancellation might fail if a trade fulfills and removes the
 * order before this function attempts to cancel it.
 */


void send_cancel_info(orderid_t buyer, orderid_t seller, quantity_t quantity){
	BRS_PACKET_HEADER * pkt = (BRS_PACKET_HEADER *) Malloc(sizeof(BRS_PACKET_HEADER));
	if(pkt == NULL){
		return;
	}
	pkt->size = htons(sizeof(BRS_NOTIFY_INFO));
	pkt->type = BRS_CANCELED_PKT;
	BRS_NOTIFY_INFO * boi = (BRS_NOTIFY_INFO *) Malloc(sizeof(BRS_NOTIFY_INFO));
	if(boi == NULL){
		return;
	}
	boi->quantity = htonl(quantity);
	trader_broadcast_packet(pkt,boi);
	Free(pkt);
	Free(boi);
}
int exchange_cancel(EXCHANGE *xchg, TRADER *trader, orderid_t order,quantity_t *quantity){

	pthread_mutex_lock(&matchmakerlock);
	int f=-1;
	BUY_ORDER_T *bo = xchg->buy_orders;
	BUY_ORDER_T *bcur = bo->next;
	while(bcur != bo){
		//during logout and login return same trader pointer
		if(order == bcur->orderid){
			f=0;
			if(trader == bcur->trader){
				*quantity = bcur->inventory;
				debug("removing buy order [%d] ",order);
				remove_b_order(bcur);
				debug("removed buy order [%d] ",order);
				send_cancel_info(order,0,*quantity);
				pthread_cond_signal(&lockcond);
				pthread_mutex_unlock(&matchmakerlock);
				return 0;
			}
		}
		bcur = bcur->next;
	}
	SELL_ORDER_T *so = xchg->sell_orders;
	SELL_ORDER_T *scur = so->next;
	while(scur != so){
		debug("given orderid %d,  list order id %d", order, scur->orderid);
		if(order == scur->orderid){
			f=0;
			if(trader == scur->trader){
				*quantity = scur->inventory;
				debug("removing sell order [%d] ",order);
				remove_s_order(scur);
				debug("removed sell order [%d] ",order);
				send_cancel_info(0,order,*quantity);
				pthread_cond_signal(&lockcond);
				pthread_mutex_unlock(&matchmakerlock);
				return 0;
			}
		}
		scur = scur->next;
	}
	if(f==-1){
		debug("order not found");
	}else{
		debug("unauthorized trader");
	}
	pthread_cond_signal(&lockcond);
	pthread_mutex_unlock(&matchmakerlock);
	return -1;
}

//FIFO
void remove_b_order(BUY_ORDER_T *cur){
	cur->prev->next = cur->next;
	cur->next->prev= cur->prev;
	debug("freeing buy order");
	Free(cur);
}

void remove_s_order(SELL_ORDER_T *cur){
	cur->prev->next = cur->next;
	cur->next->prev= cur->prev;
	debug("freeing sell order");
	Free(cur);
}

//add to last
void addto_buyorder(BUY_ORDER_T *bo, EXCHANGE *xchg){
	BUY_ORDER_T * cur = xchg->buy_orders->prev;
	BUY_ORDER_T *t = cur->next; //which is = xchg->buy_orders
	cur->next = bo;
	bo->prev = cur;
	bo->next = t;
	t->prev = bo;
	debug("order id [%d] added to the last of [buy] list", xchg->buy_orders->prev->orderid);
}

void addto_sellorder(SELL_ORDER_T *so, EXCHANGE *xchg){
	SELL_ORDER_T * cur = xchg->sell_orders->prev;
	SELL_ORDER_T *t = cur->next;
	cur->next = so;
	so->prev = cur;
	so->next = t;
	t->prev = so;
	debug("order id [%d] added to the last of [sell] list", xchg->sell_orders->prev->orderid);
}
