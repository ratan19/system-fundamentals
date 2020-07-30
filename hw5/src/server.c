#include "server.h"
#include "csapp.h"
#include "client_registry.h"
#include "protocol.h"
#include "exchange.h"
#include "debug.h"
#include "trader.h"

//NOTE DO NOT RETURN BEFORE RELEASING LOCK
extern EXCHANGE *exchange;
extern CLIENT_REGISTRY *client_registry;

void *brs_client_service(void *arg){
	debug("brs client start");
	pthread_detach(pthread_self());
	int fd = *((int *) arg);
	Free(arg);
	creg_register(client_registry, fd);
	BRS_PACKET_HEADER *rcvhdr;
	TRADER *trader;
	int rs;
	while(1){
		rcvhdr = (BRS_PACKET_HEADER *) Malloc(sizeof(BRS_PACKET_HEADER));
		if(rcvhdr == NULL){
			return NULL;
		}
		void **rcvpayloadp = Malloc(sizeof(void *));
		rs = proto_recv_packet(fd, rcvhdr, rcvpayloadp);
		if(rs < 0){
			debug("error on receiving. checking if user was logged in.");
			if(trader != NULL){
				debug("receive error for logged in trader. initiate cleanup");
				creg_unregister(client_registry,fd);
				trader_logout(trader);
			}
			Free(rcvhdr);
			Free(rcvpayloadp);
			return NULL;
		}
		if(rcvhdr->type == BRS_LOGIN_PKT){
			debug("received packet of type : [login]");
			BRS_PACKET_HEADER *sndhdr;
			sndhdr = (BRS_PACKET_HEADER *) Malloc(sizeof(BRS_PACKET_HEADER));
			int psz = (rcvhdr->size);
			char *uname = (char *) Malloc(psz+1);
			strncpy( uname, *rcvpayloadp , psz);
			*(uname+ psz) = '\0';
			trader = trader_login(fd, uname);
			trader_ref(trader, "trader ref count for the server");
			Free(uname);
			if(trader == NULL){
				//1 byte no htons req
				sndhdr->type = BRS_NACK_PKT;
				rs = proto_send_packet(fd, sndhdr, NULL);
				if(rs < 0){
					Free(sndhdr);
					Free(rcvhdr);
					Free(rcvpayloadp);
					return NULL;
				}
				Free(sndhdr);
				Free(rcvhdr);
				Free(rcvpayloadp);
				continue;
			}
			sndhdr->type = BRS_ACK_PKT;
			rs = proto_send_packet(fd, sndhdr, NULL);
			// Free(sndhdr);
			if(rs < 0){
				Free(sndhdr);
				Free(rcvhdr);
				Free(rcvpayloadp);
				return NULL;
			}
			Free(sndhdr);
			Free(rcvhdr);
			Free(rcvpayloadp);
			continue;
		}
		if(trader != NULL){
			if(rcvhdr->type == BRS_LOGIN_PKT){
				// Free(sndhdr);
				Free(rcvhdr);
				Free(rcvpayloadp);
				continue;
			}

			else if(rcvhdr->type == BRS_STATUS_PKT){

				BRS_STATUS_INFO *status = Malloc(sizeof(BRS_STATUS_INFO));
				exchange_get_status(exchange, status);
				rs = trader_send_ack(trader, (void *)status);
				if(rs < 0){
					Free(rcvhdr);
					Free(status);
					Free(rcvpayloadp);
					return NULL;
				}
				Free(rcvhdr);
				Free(status);
				Free(rcvpayloadp);
				continue;
			}
			else if(rcvhdr->type == BRS_DEPOSIT_PKT){
				debug("received packet of type : [deposit]");
				int * dp = *rcvpayloadp;
				int depst = ntohl(*dp);
				trader_increase_balance(trader, depst);
				BRS_STATUS_INFO *status = Malloc(sizeof(BRS_STATUS_INFO));
				exchange_get_status(exchange, status);
				rs = trader_send_ack(trader, (void *)status);
				if(rs < 0){
					Free(rcvhdr);
					Free(status);
					Free(rcvpayloadp);
					return NULL;
				}
				Free(rcvhdr);
				Free(status);
				Free(rcvpayloadp);
				continue;
			}
			else if(rcvhdr->type == BRS_WITHDRAW_PKT){
				debug("received packet of type : [withdraw]");
				int * wthp = *rcvpayloadp;
				int wthd = ntohl(*wthp);
				int r = trader_decrease_balance(trader, wthd);
				if(r == -1){;
					trader_send_nack(trader);
					Free(rcvhdr);
					Free(rcvpayloadp);
					continue;
				}

				BRS_STATUS_INFO *status = Malloc(sizeof(BRS_STATUS_INFO));
				exchange_get_status(exchange, status);
				rs = trader_send_ack(trader, (void *)status);
				if(rs < 0){
					Free(rcvhdr);
					Free(status);
					Free(rcvpayloadp);
					return NULL;
				}
				Free(rcvhdr);
				Free(status);
				Free(rcvpayloadp);
				continue;
			}
			else if(rcvhdr->type == BRS_ESCROW_PKT){
				debug("received packet of type : [escrow]");
				int * qt = *rcvpayloadp;
				int quantity = ntohl(*qt);
				debug("quantity escrowed by: %d", quantity);
				trader_increase_inventory(trader, quantity);

				BRS_STATUS_INFO *status = Malloc(sizeof(BRS_STATUS_INFO));
				exchange_get_status(exchange, status);
				rs = trader_send_ack(trader, (void *)status);
				if(rs < 0){
					Free(rcvhdr);
					Free(status);
					Free(rcvpayloadp);
					return NULL;
				}
				Free(rcvhdr);
				Free(status);
				Free(rcvpayloadp);
				continue;
			}
			else if(rcvhdr->type == BRS_RELEASE_PKT){
				debug("received packet of type : [release]");
				int * relp = *rcvpayloadp;
				int relqt = ntohl(*relp);
				int r = trader_decrease_inventory(trader, relqt);
				if(r == -1){
					trader_send_nack(trader);
					Free(rcvhdr);
					Free(rcvpayloadp);
					continue;
				}

				BRS_STATUS_INFO *status = Malloc(sizeof(BRS_STATUS_INFO));
				exchange_get_status(exchange, status);
				rs = trader_send_ack(trader, (void *)status);

				if(rs < 0){
					Free(rcvhdr);
					Free(status);
					Free(rcvpayloadp);
					return NULL;
				}
				Free(rcvhdr);
				Free(status);
				Free(rcvpayloadp);
				continue;
			}
			else if(rcvhdr->type == BRS_BUY_PKT){
				debug("received packet of type : [buy]");
				BRS_ORDER_INFO * buyp =(BRS_ORDER_INFO *) (*rcvpayloadp);
				int quantity = ntohl(buyp->quantity);
				int price = ntohl(buyp->price);
				orderid_t oid = exchange_post_buy(exchange, trader, quantity, price);
				// orderid_t oid2 =
				// exchange_post_buy(exchange, trader, quantity+1, price+1);
				debug("buy order-id  : [%d] for trader %p", oid, trader);
				BRS_STATUS_INFO *status = Malloc(sizeof(BRS_STATUS_INFO));

				exchange_get_status(exchange, status);
				status->orderid = htonl(oid);
				rs = trader_send_ack(trader, (void *)status);
				if(rs < 0){
					debug("error in sending the ack");
					Free(rcvhdr);
					Free(status);
					Free(rcvpayloadp);
					return NULL;
				}
				Free(rcvhdr);
				Free(status);
				Free(rcvpayloadp);
				continue;
			}
			else if(rcvhdr->type == BRS_SELL_PKT){
				debug("received packet of type : [sell]");
				BRS_ORDER_INFO * buyp =(BRS_ORDER_INFO *) (*rcvpayloadp);
				int quantity = ntohl(buyp->quantity);
				int price = ntohl(buyp->price);
				int r = trader_decrease_inventory(trader, quantity);
				if(r == -1){
					debug("sell error");
					trader_send_nack(trader);
					Free(rcvhdr);
					Free(rcvpayloadp);
					continue;
				}
				orderid_t oid = exchange_post_sell(exchange, trader, quantity, price);
				debug("sell order-id  : [%d] for trader %p", oid, trader);
				BRS_STATUS_INFO *status = Malloc(sizeof(BRS_STATUS_INFO));
				status->orderid = htonl(oid);
				exchange_get_status(exchange, status);
				rs = trader_send_ack(trader, (void *)status);
				if(rs < 0){
					Free(rcvhdr);
					Free(status);
					Free(rcvpayloadp);
					return NULL;
				}
				Free(rcvhdr);
				Free(status);
				Free(rcvpayloadp);
				continue;
			}
			else if(rcvhdr->type == BRS_CANCEL_PKT){
				debug("received packet of type : [cancel]");
				BRS_CANCEL_INFO * cnclp =(BRS_CANCEL_INFO *) (*rcvpayloadp);
				orderid_t oid = ntohl(cnclp->order);
				debug("oid: %d",oid);
		    	quantity_t *quantity=(quantity_t *)Malloc(sizeof(quantity_t));
				int res = exchange_cancel(exchange, trader, oid, quantity);
				debug("canceled from exc");
				if(res == -1){
					debug("ret -1 from exc");
					rs = trader_send_nack(trader);
					// debug("freedrcvhdr from exc");
					Free(rcvhdr);
					Free(rcvpayloadp);
					Free(quantity);
					continue;
				}
				BRS_STATUS_INFO *status = Malloc(sizeof(BRS_STATUS_INFO));
				exchange_get_status(exchange, status);
				status->orderid = htonl(oid);
				status->quantity=*quantity;
				rs = trader_send_ack(trader, (void *)status);
				if(rs < 0){
					Free(rcvhdr);
					Free(status);
					Free(rcvpayloadp);
					Free(quantity);
					return NULL;
				}
				Free(rcvhdr);
				Free(status);
				Free(rcvpayloadp);
				Free(quantity);
				continue;
			}
		}
	}
}



