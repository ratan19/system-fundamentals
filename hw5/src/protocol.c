#include "protocol.h"
#include "debug.h"
#include "csapp.h"

int proto_send_packet(int fd, BRS_PACKET_HEADER *hdr, void *payload){
	int res = rio_writen(fd, hdr, sizeof(BRS_PACKET_HEADER));
	if(res != sizeof(BRS_PACKET_HEADER)){
		debug("error on writing to fd");
		debug("written bytes: %d , actual bytes %ld",res,sizeof(BRS_PACKET_HEADER));
		return -1;
	}
	if(payload == NULL){
		// debug("empty packet sent");
		return 0;
	}
	uint16_t sz = ntohs(hdr->size);
	if(sz < 0){
		debug("negative ntohs res");
		return -1;
	}
	res = rio_writen(fd, payload, sz);
	if(res != sz){
		debug("error on writing to fd");
		return -1;
	}
	return 0;
}


int proto_recv_packet(int fd, BRS_PACKET_HEADER *hdr, void **payloadp){
	int res = rio_readn(fd, hdr, sizeof(BRS_PACKET_HEADER));
	if( res != sizeof(BRS_PACKET_HEADER)){
		debug("error reading from fd.");
		debug("read bytes: %d , actual bytes %ld ",res, sizeof(BRS_PACKET_HEADER));
		//set errno
		return -1;
	}
	hdr->size = ntohs(hdr->size);
	hdr->timestamp_sec=ntohl(hdr->timestamp_sec);
	hdr->timestamp_nsec=ntohl(hdr->timestamp_nsec);
	if(hdr->size == 0){
		return 0;
	}
	void * tpload = (void *) Malloc(hdr->size);
	res = rio_readn(fd, tpload, hdr->size);
	if( res != hdr->size ){
		debug("error reading from fd.");
		//set errno
		return -1;
	}
	*payloadp = tpload;
	return 0;
}