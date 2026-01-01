#include "queue.h"
#include <stdlib.h>

int queue_init(Queue *q, size_t cap) {
    q->buf = (Commande*)calloc(cap, sizeof(Commande));
    if (!q->buf) return -1;
    q->cap = cap;
    q->head = q->tail = q->count = 0;
    return 0;
}

void queue_destroy(Queue *q) {
    free(q->buf);
    q->buf = NULL;
    q->cap = q->head = q->tail = q->count = 0;
}

int queue_push(Queue *q, Commande c) {
    if (q->count >= q->cap) return -1;
    q->buf[q->tail] = c;
    q->tail = (q->tail + 1) % q->cap;
    q->count++;
    return 0;
}

int queue_pop(Queue *q, Commande *out) {
    if (q->count == 0) return -1;
    *out = q->buf[q->head];
    q->head = (q->head + 1) % q->cap;
    q->count--;
    return 0;
}
