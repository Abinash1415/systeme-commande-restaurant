#ifndef QUEUE_H
#define QUEUE_H

#include <stddef.h>

typedef enum {
    PLAT_PIZZA = 0,
    PLAT_BURGER,
    PLAT_SUSHI,
    PLAT_PATES,
    PLAT_SALADE,
    PLAT_COUNT
} Plat;

typedef struct {
    int id;                 // id commande
    Plat plat;              // type de plat
    int prep_ms;            // temps de "préparation"
} Commande;

typedef struct {
    Commande *buf;
    size_t cap;
    size_t head;
    size_t tail;
    size_t count;
} Queue;

int  queue_init(Queue *q, size_t cap);
void queue_destroy(Queue *q);

int  queue_push(Queue *q, Commande c);   // suppose : mutex tenu + place dispo
int  queue_pop(Queue *q, Commande *out); // suppose : mutex tenu + item dispo

#endif
