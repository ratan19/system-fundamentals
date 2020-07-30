/**
 * All functions you make for the assignment must be implemented in this file.
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debug.h"
#include "sfmm.h"
#include "structhelper.h"
#include <errno.h>

#define PROLOGUE_BLOCK_SIZE 32
#define EPILOGUE_BLOCK_SIZE 0

void *sf_malloc(size_t size) {
	if( size == 0){
		return NULL;
	}

	int require_epilogue_shift = 0;
	if(sf_mem_start() == sf_mem_end()){
		init_freelist();
		void * block_pointer = sf_mem_grow();

		if(block_pointer == NULL){
			sf_errno = ENOMEM;
			return NULL;
		}

		allocate_prologue();

		sf_block * sf_block_p = (sf_block *)shift_ptr(block_pointer,32, INC);
		int prev_alloc  = (int)((sf_block_p->prev_footer ^ sf_magic())& PREV_BLOCK_ALLOCATED);
		int alloc = NOT_ALLOCATED;//0;
		size_t blk_sz = PAGE_SZ - 48;
		set_header_footer(sf_block_p, blk_sz, alloc, prev_alloc);
		addto_freelist(sf_block_p);
		allocate_epilogue();
	}

	size_t blk_sz = effective_blk_sz(size);
	void *  temp = search_free_list_heads( blk_sz);

	while (temp == NULL){
		void * nxt_page_p = sf_mem_grow();
		require_epilogue_shift = 1;

		if(nxt_page_p == NULL){
			sf_errno = ENOMEM;
			return NULL;
		}

		sf_block * sf_block_p = (sf_block *) (shift_ptr(nxt_page_p, 16, DCR));
		int prev_alloc = (sf_block_p->prev_footer ^ sf_magic()) & THIS_BLOCK_ALLOCATED;
		if(prev_alloc == 2){
			prev_alloc = PREV_BLOCK_ALLOCATED;
		}else{
			prev_alloc = NOT_ALLOCATED;
		}

		set_header_footer(sf_block_p, PAGE_SZ, NOT_ALLOCATED, prev_alloc);
		allocate_epilogue();

		sf_block * t = coalesce_h(sf_block_p,0);

		if(t != sf_block_p){
			sf_block_p = t;
		}

		addto_freelist(sf_block_p);
		temp = search_free_list_heads( blk_sz);
		allocate_epilogue();
	}

	sf_block * sf_block_p = (sf_block *) temp;
	size_t received_blk_size = sf_block_p->header & BLOCK_SIZE_MASK;
	int alloc = THIS_BLOCK_ALLOCATED;//TRUE_B;//1;
	int prev_alloc = sf_block_p->header & PREV_BLOCK_ALLOCATED;
	sf_block * next_sf_block_p = set_header_footer(sf_block_p,received_blk_size, alloc, prev_alloc);

	size_t nxt_sz = next_sf_block_p->header & BLOCK_SIZE_MASK;
	int nxt_alloc = next_sf_block_p->header & THIS_BLOCK_ALLOCATED;
	int nxt_prev_alloc;
	if(alloc == THIS_BLOCK_ALLOCATED){
		nxt_prev_alloc = PREV_BLOCK_ALLOCATED;
	}else{
		nxt_prev_alloc = NOT_ALLOCATED;
	}

	set_header_footer(next_sf_block_p,nxt_sz, nxt_alloc, nxt_prev_alloc);

	allocate_epilogue();
	if(require_epilogue_shift == 1){
		allocate_epilogue();
	}

    return &(sf_block_p->body.payload);
}

void sf_free(void *pp) {
	int is_valid = validate_pointer(pp);

	if(is_valid == 0){
		sf_errno = EINVAL;
		// printf("aborting , sf_error: %d\n", sf_errno);
		abort();
	}

	//set header alloc field to 0 and then coalesce
	sf_block * sf_block_p = (sf_block *) (shift_ptr(pp,16,DCR));
	int prev_alloc = sf_block_p->header & PREV_BLOCK_ALLOCATED;
	int alloc = sf_block_p->header & THIS_BLOCK_ALLOCATED;
	size_t blk_sz = sf_block_p->header & BLOCK_SIZE_MASK;

	if(alloc == 0){
		abort();
	}

	alloc = NOT_ALLOCATED;//0;
	sf_block * nxt_sf_block_p = (sf_block * ) set_header_footer(sf_block_p,blk_sz,alloc,prev_alloc);

	//updates the prev alloc bit only
	update_nxt_block(nxt_sf_block_p);
	sf_block * res_p = coalesce(sf_block_p);
	addto_freelist(res_p);
    return;
}

void *sf_realloc(void *pp, size_t rsize) {
	int is_valid = validate_pointer(pp);

	if(is_valid == 0){
		sf_errno = EINVAL;
		// printf("\naborting , sf_error: %d\n", sf_errno);
		abort();
	}

	if(rsize == 0){
		sf_free(pp);
		return NULL;
	}

	sf_block * sf_block_p = (sf_block *) (shift_ptr(pp, 16, DCR));
	size_t blk_sz = sf_block_p->header & BLOCK_SIZE_MASK;

	size_t req_blk_sz = effective_blk_sz(rsize);

	if(req_blk_sz == blk_sz ){
		return pp;
	}

	size_t payload_size = blk_sz - 16;

	if(req_blk_sz > blk_sz){
		void * allocated_payload_p = sf_malloc(rsize);

		if(allocated_payload_p == NULL){
			sf_errno = ENOMEM;
			return NULL;
		}

		void * src_payload_p = (void *) (sf_block_p->body.payload);
		memcpy(allocated_payload_p, src_payload_p, payload_size);
		sf_free(pp);
		return allocated_payload_p;
	}
	else{
		if(blk_sz - req_blk_sz < 32){
			return pp;
		}
		else{
			split_block(sf_block_p,req_blk_sz);
			return (void *)sf_block_p->body.payload;
		}
	}
}

size_t effective_blk_sz(size_t payload){
	size_t size = payload + 8l + 8l;

	if(size % 16 == 0){
		return size;
	}

	size = ((size/16))* 16 + 16;
	return size < 32 ?  32 : size;
}

//pack doesnt take actual value of alloc. if alloc it takes 1 otherwise 0
size_t pack(size_t size, int alloc, int prev_alloc){
	return (size | alloc | prev_alloc);
}

void allocate_prologue(){
	sf_prologue * prologue_p = (sf_prologue *) sf_mem_start();
	int alloc = THIS_BLOCK_ALLOCATED;//TRUE_B;//1;
	int prev_alloc = PREV_BLOCK_ALLOCATED;//TRUE_B;//1;
	prologue_p->header =  pack(PROLOGUE_BLOCK_SIZE,alloc,prev_alloc);
	prologue_p->footer = prologue_p->header ^ sf_magic();
}
void allocate_epilogue(){
	sf_epilogue * epilogue_p = (sf_epilogue *)sf_mem_end();
	epilogue_p = epilogue_p - 1;
	int alloc = THIS_BLOCK_ALLOCATED;//TRUE_B;//1;
	footer * footer_p = (footer *) shift_ptr(epilogue_p, 8, DCR);
	int prev_alloc  = (int) ((footer_p->content ^ sf_magic()) & THIS_BLOCK_ALLOCATED);
	if(prev_alloc == 2){
		prev_alloc = PREV_BLOCK_ALLOCATED;
	}
	else{
		prev_alloc = NOT_ALLOCATED;
	}
	epilogue_p->header = pack(EPILOGUE_BLOCK_SIZE, alloc, prev_alloc);
}

void * shift_ptr(void *ptr ,size_t shift, int incr){
	char * p = (char *) ptr;
	while(shift != 0){
		if(incr == 0){
			p--;
		}
		else{
			p++;
		}
		shift--;
	}
	return (void *)p;
}

int freelisthead_index(size_t blk_sz){
	if(blk_sz == 32){
		return 0;
	}else if(blk_sz > 32 && blk_sz <= 64){
		return 1;
	}else if(blk_sz > 64 && blk_sz <= 128){
		return 2;
	}else if(blk_sz > 128 && blk_sz <= 256){
		return 3;
	}else if(blk_sz > 256 && blk_sz <= 512){
		return 4;
	}else if(blk_sz > 512 && blk_sz <= 1024){
		return 5;
	}else if(blk_sz > 1024 && blk_sz <= 2048){
		return 6;
	}else if(blk_sz > 2048 && blk_sz <= 4096){
		return 7;
	}else{
		return 8;
	}
}

void * search_free_list_heads(size_t blk_sz){
	int index = freelisthead_index(blk_sz);

	while(index < NUM_FREE_LISTS){
		void * temp = search_free_list(index,blk_sz);

		if(temp != NULL){
			sf_block * sf_block_p = (sf_block *) temp;
			return sf_block_p;
		}

		index++;
	}

	return NULL;
}

void * search_free_list(int index, size_t req_size){
	sf_block * res_p = NULL;
	sf_block * prev_sf_block_p= &sf_free_list_heads[index];
	sf_block * nxt_sf_block_p = (prev_sf_block_p->body).links.next;

	if(prev_sf_block_p == nxt_sf_block_p){
		return NULL;
	}

	while(prev_sf_block_p != nxt_sf_block_p){
		size_t available_size = nxt_sf_block_p->header & BLOCK_SIZE_MASK;

		if(available_size >= req_size){

			if(available_size - req_size >= 32){
				split_block(nxt_sf_block_p,req_size);
			}

			res_p = nxt_sf_block_p;
			remove_freelist_link(nxt_sf_block_p);
			break;
		}

		nxt_sf_block_p = (nxt_sf_block_p->body).links.next;
	}

	return res_p;
}

void remove_freelist_link(sf_block *nxt_sf_block_p){
sf_block * tmpnxt = (nxt_sf_block_p->body).links.next;

sf_block * temp = (nxt_sf_block_p->body).links.prev;

(temp->body).links.next = tmpnxt;
(tmpnxt->body).links.prev = temp;

(nxt_sf_block_p->body).links.prev = NULL;
(nxt_sf_block_p->body).links.next = NULL;
}

void addto_freelist( sf_block * sf_block_p){
	//NOTE: coalescing done before calling this
	size_t blk_sz = sf_block_p->header & BLOCK_SIZE_MASK;
	int index = freelisthead_index(blk_sz);
	//LIFO order
	sf_block * nxt_sf_block_p = sf_free_list_heads[index].body.links.next;
	sf_block_p->body.links.next = sf_free_list_heads[index].body.links.next;
	sf_block_p->body.links.prev  = &sf_free_list_heads[index];
	sf_free_list_heads[index].body.links.next = sf_block_p;
	nxt_sf_block_p->body.links.prev = sf_block_p;
	return;
}

void init_freelist(){

	for( int i=0; i<NUM_FREE_LISTS ;i++){
		sf_free_list_heads[i].body.links.next = &(sf_free_list_heads[i]);
		sf_free_list_heads[i].body.links.prev = &(sf_free_list_heads[i]);
	}

}

//call to this assumes req_size is less than block size of sf_block_p
void split_block(sf_block * sf_block_p, size_t req_size){
	size_t original_size = (sf_block_p->header) & BLOCK_SIZE_MASK;

	//IMP NOTE: alloc should be 2 when split called from either free block or allocated block
	int alloc = THIS_BLOCK_ALLOCATED;//TRUE_B;//1;

	int prev_alloc = sf_block_p->header & PREV_BLOCK_ALLOCATED;

	//new header for allocated block
	sf_block * sf_block_nxt_p = (sf_block *) set_header_footer(sf_block_p, req_size, alloc, prev_alloc);

	//work on free block data
	alloc = NOT_ALLOCATED;//0;
	prev_alloc = PREV_BLOCK_ALLOCATED;//TRUE_B;//1;
	size_t freeblk_sz = original_size-req_size;
	sf_block * sf_block_nxt_nxt_p = (sf_block *) set_header_footer(sf_block_nxt_p, freeblk_sz, alloc, prev_alloc);

	//attempt coalesce with nxt block. Split will always have prev block alloc so only coalesce next
	sf_block * temp = coalesce_h(sf_block_nxt_nxt_p, 1);

	if( sf_block_nxt_nxt_p != temp){
		sf_block_nxt_p = temp;
		size_t nxt_sz = sf_block_nxt_p->header & BLOCK_SIZE_MASK;
		int nxt_alloc = sf_block_nxt_p->header & THIS_BLOCK_ALLOCATED;

		set_header_footer(sf_block_nxt_p,nxt_sz, nxt_alloc, PREV_BLOCK_ALLOCATED);
	}
	else{
		size_t nxt_sz = sf_block_nxt_nxt_p->header & BLOCK_SIZE_MASK;
		int nxt_alloc = sf_block_nxt_nxt_p->header & THIS_BLOCK_ALLOCATED;
		int nxt_prev_alloc;
		if(alloc == THIS_BLOCK_ALLOCATED){
			nxt_prev_alloc = PREV_BLOCK_ALLOCATED;
		}else{
			nxt_prev_alloc = NOT_ALLOCATED;
		}

		set_header_footer(sf_block_nxt_nxt_p,nxt_sz, nxt_alloc, nxt_prev_alloc);
	}

	addto_freelist(sf_block_nxt_p);
}

sf_block * coalesce_h(sf_block * sf_block_p, int nxt_block){

	if(sf_block_p == sf_mem_end()-8){
		return sf_block_p;
	}
	size_t prev_blk_sz;
	size_t current_blk_sz = 0;
	current_blk_sz = sf_block_p->header & BLOCK_SIZE_MASK;
	int is_current_block_alloc = sf_block_p->header & THIS_BLOCK_ALLOCATED;
	prev_blk_sz = ((sf_block_p->prev_footer ^ sf_magic()) & BLOCK_SIZE_MASK);

	//check if free
	int is_prev_block_alloc = (sf_block_p->prev_footer ^ sf_magic()) & THIS_BLOCK_ALLOCATED;

	//will never coalesce prologue and epilogue if their alloc are 1
	if(! (is_prev_block_alloc == 0 && is_current_block_alloc == 0) ){
		return sf_block_p;
	}

	sf_block * prev_sf_block_p = (sf_block *) shift_ptr(sf_block_p, prev_blk_sz, DCR);
	int alloc;
	int prev_alloc;

	alloc = prev_sf_block_p->header & THIS_BLOCK_ALLOCATED;
	prev_alloc = prev_sf_block_p->header & PREV_BLOCK_ALLOCATED;

	if(nxt_block == 1){
		remove_freelist_link(sf_block_p);
	}
	else{
		remove_freelist_link(prev_sf_block_p);
	}

	size_t new_blk_sz = current_blk_sz + prev_blk_sz;
	set_header_footer(prev_sf_block_p,new_blk_sz, alloc, prev_alloc);
	return prev_sf_block_p;
}

sf_block * coalesce(sf_block * sf_block_p){
	//first attempt coalesce with nxt block
	size_t current_blk_sz = sf_block_p->header & BLOCK_SIZE_MASK;
	sf_block * nxt_sf_block_p = (sf_block *) shift_ptr(sf_block_p, current_blk_sz, INC);
	sf_block * temp = coalesce_h(nxt_sf_block_p, 1);

	if( temp != nxt_sf_block_p){
		sf_block_p = temp;
	}

	//attempt coalesce with prev block
	sf_block * res_p = coalesce_h(sf_block_p, 0);
	return res_p;
}

void * set_footer(sf_block * sf_block_p, size_t blk_sz){
	size_t header = sf_block_p->header;
	footer * footer_p = (footer *) shift_ptr(sf_block_p, blk_sz, INC);

	footer_p->content = (header ^ sf_magic());
	return footer_p;
}

int validate_pointer(void * pp){

	if(pp == NULL){
		// printf("null pointer");
		return 0;
	}

	sf_block * sf_block_p = (sf_block *) (shift_ptr(pp, 16, DCR));
	void * header_p = (void *)&(sf_block_p->header);

	if(header_p < (sf_mem_start() + 40)){
		// printf("header_p before prologue_p");
		return 0;
	}
	size_t blk_sz = sf_block_p->header & BLOCK_SIZE_MASK;
	void * footer_p = shift_ptr(sf_block_p, blk_sz, INC);

	if(footer_p > sf_mem_end() - 8){
		// printf("footer beyond epilogue_p");
		return 0;
	}

	if(blk_sz < 32){
		// printf("block less than 32");
		return 0;
	}

	int alloc = sf_block_p->header & THIS_BLOCK_ALLOCATED;
	if(alloc == 0){
		// printf("block to be freed has alloc = 0");
		return 0;
	}

	int prev_alloc = sf_block_p->header & PREV_BLOCK_ALLOCATED;
	int actual_prev_alloc = (sf_block_p->prev_footer ^ sf_magic()) & THIS_BLOCK_ALLOCATED;

	//TODO: check this again
	// if(!(prev_alloc == 1 && actual_prev_alloc == 2)){
	if((prev_alloc << 1) != actual_prev_alloc){
		// printf("prev allocs not equal");
		return 0;
	}

	if((((footer *)footer_p)->content ^ sf_magic() ) != sf_block_p->header ){
		// printf("header footer");
		return 0;
	}
	return 1;
}

void * set_header_footer(sf_block *sf_block_p, size_t blk_sz, int alloc, int prev_alloc){

	size_t hdr_val = pack(blk_sz, alloc, prev_alloc);
	sf_block_p->header = hdr_val;
	sf_block * nxt_sf_block_p = set_footer(sf_block_p, blk_sz);
	return nxt_sf_block_p;
}

void update_nxt_block(sf_block * nxt_sf_block_p){
	int alloc = (nxt_sf_block_p->prev_footer ^ sf_magic()) & THIS_BLOCK_ALLOCATED;

	if(((void *)nxt_sf_block_p + 8) > sf_mem_end()){
		return;
	}

	else if(((void *)nxt_sf_block_p + 8) == sf_mem_end()){
		allocate_epilogue();
		return;
	}

	size_t nxt_blk_sz = nxt_sf_block_p->header & BLOCK_SIZE_MASK;
	int nxt_block_alloc = nxt_sf_block_p->header & THIS_BLOCK_ALLOCATED;
	int nxt_block_prev_alloc = alloc;

	int nxt_alloc = nxt_block_alloc;
	size_t hdr_val = pack(nxt_blk_sz,nxt_alloc,nxt_block_prev_alloc);
	nxt_sf_block_p->header = hdr_val;
	footer * footer_p = shift_ptr(nxt_sf_block_p, nxt_blk_sz, INC);
	footer_p->content = nxt_sf_block_p->header ^ sf_magic();
}


int validate_block(){
	int flag = 0;
	sf_block * prl = ( sf_block *) sf_mem_start();

	size_t blk_size = prl->header & BLOCK_SIZE_MASK;
	int alloc = prl->header  & THIS_BLOCK_ALLOCATED;
	int prev_alloc = prl->header  & PREV_BLOCK_ALLOCATED;

	if(blk_size != 32 || alloc != THIS_BLOCK_ALLOCATED || prev_alloc != PREV_BLOCK_ALLOCATED){
		printf("prologue header corrupted\n");
		sf_show_blocks();
			flag = 1;

	}

	sf_block * prl_f = (sf_block *) shift_ptr(prl, 32, INC);

	if(prl_f->prev_footer != (sf_magic() ^ prl->header)){
		printf("prologue header footer mismatch\n");
	}

	sf_block * curr_block = (sf_block *) prl_f;
	while( ((char *)curr_block) != ((char *)sf_mem_end()) - 16 ){
		if(((char *)curr_block) >= ((char *)sf_mem_end()) - 8){
			printf("block pointer beyound epilogue\n");
			sf_show_blocks();
			flag = 1;
			return flag;
		}

		int prev_blk_alloc = (curr_block->prev_footer ^ sf_magic()) & THIS_BLOCK_ALLOCATED;
		int curr_block_prev_alloc = curr_block->header & PREV_BLOCK_ALLOCATED;
		size_t bloc_size = curr_block->header & BLOCK_SIZE_MASK;
		size_t curr_block_header = curr_block->header;

		if((curr_block_prev_alloc << 1) != prev_blk_alloc){
			printf("prev alloc dont match\n");
			sf_show_blocks();
			flag = 1;

		}

		curr_block = (sf_block *) shift_ptr(curr_block, bloc_size, INC);

		if(curr_block_header != (curr_block->prev_footer ^ sf_magic()) ){
			printf("header footer not xored\n");
			sf_show_blocks();
			flag = 1;
		}

	}


	size_t epi_h = curr_block->header;
	size_t bloc_size = epi_h & BLOCK_SIZE_MASK;
	alloc = epi_h & THIS_BLOCK_ALLOCATED;

	if(bloc_size != 0 || alloc != 2){
		printf("epilogue corrupted\n");
		sf_show_blocks();
			flag = 1;

	}
	prev_alloc = epi_h & PREV_BLOCK_ALLOCATED;

	if(((curr_block->prev_footer ^ sf_magic()) & THIS_BLOCK_ALLOCATED) != prev_alloc << 1 ){
		printf("epilogue prev alloc not matching\n");
		sf_show_blocks();
			flag = 1;

	}
	if(flag == 1){
		printf("=====\n");
		printf("ERROR\n");
		printf("=====\n");
	}
	return flag;

}