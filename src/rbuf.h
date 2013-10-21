#include <stdint.h>

typedef struct __rbuf rbuf_t;
typedef struct __rbuf_page rbuf_page_t;

typedef void (*rbuf_free_value_callback_t)(void *v);
#pragma pack(push, 4)
struct __rbuf_page {
    void               *value;
    struct __rbuf_page *next; 
    struct __rbuf_page *prev; 
};

struct __rbuf {            
    rbuf_page_t             *head;
    rbuf_page_t             *tail;
    rbuf_page_t             *commit;
    rbuf_page_t             *reader;
    rbuf_free_value_callback_t free_value_cb;
    uint32_t                size;
    int                     overwrite_mode;
    uint32_t                writes;
};
#pragma pack(pop)


int rb_write(rbuf_t *rb, void *value);
void *rb_read(rbuf_t *rb);
void rb_destroy(rbuf_t *rb);
rbuf_t *rb_create(uint32_t size);
rbuf_page_t *rb_create_page();
void rb_destroy_page(rbuf_page_t *page, rbuf_free_value_callback_t free_value_cb);
void *rb_page_value(rbuf_page_t *page);
