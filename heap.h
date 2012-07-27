#ifndef __HEAP_H_INCLUDED__
#define __HEAP_H_INCLUDED__

typedef int (*cmp_func)(void *, void *);
typedef void (*record_func)(void *, int);

typedef struct heap_st {
    int         cap;
    int         len;
    void        **data;
    cmp_func    less;
    record_func record;
} heap_t;

heap_t *heap_create(void);
int heap_init(heap_t *h);
int heap_insert(heap_t *h, void *data);
void *heap_remove(heap_t *h, int k);
void heap_destroy(heap_t *h);
void heap_free(heap_t *h);

#endif /* __HEAP_H_INCLUDED__ */
