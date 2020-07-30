#define INC 1
#define DCR 0
#define NOT_ALLOCATED 0

typedef struct footer{
    size_t content : 64;
} footer;

extern size_t effective_blk_sz(size_t payload);
extern void allocate_prologue();
extern void allocate_epilogue();
extern void * shift_ptr(void *ptr ,size_t shift, int incr);
extern size_t pack(size_t size, int alloc, int prev_alloc);
extern void * search_free_list_heads(size_t blk_sz);
extern void * search_free_list(int index, size_t blk_sz);
extern void remove_freelist_link(sf_block *nxt_sf_block_p);
extern void split_block(sf_block * sf_block, size_t req_size);
extern void addto_freelist( sf_block * sf_block_p);
extern void init_freelist();
extern sf_block * coalesce(sf_block * sf_block_p);
extern void * set_footer(sf_block * sf_block_p,size_t blk_sz);
extern int validate_pointer(void * pp);
extern void * set_header_footer(sf_block *sf_block_p,size_t blk_sz, int alloc, int prev_alloc);
extern int freelisthead_index(size_t blk_sz);
extern sf_block * coalesce_h(sf_block * sf_block_p, int nxt_block);
extern void update_nxt_block(sf_block * nxt_sf_block_p);
extern int validate_block();


