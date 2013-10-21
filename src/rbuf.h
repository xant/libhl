#include <stdint.h>

typedef struct __rbuf rbuf_t;
typedef struct __rbuf_page rbuf_page_t;

typedef void (*rbuf_free_value_callback_t)(void *v);

int rb_write(rbuf_t *rb, void *value);
void *rb_read(rbuf_t *rb);
void rb_destroy(rbuf_t *rb);
rbuf_t *rb_create(uint32_t size);
rbuf_page_t *rb_create_page();
void rb_destroy_page(rbuf_page_t *page, rbuf_free_value_callback_t free_value_cb);
void *rb_page_value(rbuf_page_t *page);
uint32_t rb_write_count(rbuf_t *rb);
