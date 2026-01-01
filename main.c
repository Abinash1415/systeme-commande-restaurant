#define _POSIX_C_SOURCE 200809L

#include "queue.h"

#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <errno.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define NB_SERVEURS   3
#define NB_CUISINIERS 2
#define QUEUE_CAP     20

// =====================
// Utilitaires temps
// =====================
static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static const char* plat_name(Plat p) {
    switch (p) {
        case PLAT_PIZZA:  return "Pizza";
        case PLAT_BURGER: return "Burger";
        case PLAT_SUSHI:  return "Sushi";
        case PLAT_PATES:  return "Pâtes";
        case PLAT_SALADE: return "Salade";
        default:          return "???";
    }
}

// =====================
// Synchronisation & file
// =====================
static Queue g_queue;
static pthread_mutex_t g_mtx_queue = PTHREAD_MUTEX_INITIALIZER;
static sem_t g_freeSlots;
static sem_t g_items;

// stop propre
static volatile sig_atomic_t g_stop = 0;
static int g_next_id = 1;

// sem_wait robuste (gère EINTR)
static int sem_wait_intr(sem_t *s) {
    for (;;) {
        if (sem_wait(s) == 0) return 0;
        if (errno == EINTR) {
            if (g_stop) return -1;
            continue;
        }
        return -1;
    }
}

static void handle_sigint(int sig) {
    (void)sig;
    g_stop = 1;

    // Réveille ceux potentiellement bloqués sur sem_wait
    for (int i = 0; i < 64; i++) {
        sem_post(&g_items);
        sem_post(&g_freeSlots);
    }
}

static void install_sigint(void) {
    signal(SIGINT, handle_sigint);
}

// =====================
// Dashboard (état partagé)
// =====================
typedef struct {
    int last_id;
    Plat last_plat;
    int last_prep_ms;
    long long ts_ms;
    int produced_count;
} ServerStatus;

typedef struct {
    int busy;               // 0/1
    int current_id;
    Plat current_plat;
    int prep_ms;
    long long end_ms;       // fin prévue (now_ms + prep_ms)
    int done_count;
} CookStatus;

static pthread_mutex_t g_mtx_status = PTHREAD_MUTEX_INITIALIZER;
static ServerStatus g_srv[NB_SERVEURS];
static CookStatus   g_ck[NB_CUISINIERS];
static int g_total_produced = 0;
static int g_total_done = 0;
static int g_cooks_done_threads = 0;

// =====================
// Génération commande
// =====================
typedef struct {
    int idx;       // 1..N
    unsigned seed;
} ThreadCtx;

static Commande make_commande(ThreadCtx *ctx) {
    Commande c;
    c.id = __sync_fetch_and_add(&g_next_id, 1);
    c.plat = (Plat)(rand_r(&ctx->seed) % PLAT_COUNT);
    c.prep_ms = 500 + (int)(rand_r(&ctx->seed) % 2001); // 500..2500ms
    return c;
}

// =====================
// Threads
// =====================
static void* serveur_thread(void *arg) {
    ThreadCtx *ctx = (ThreadCtx*)arg;
    int i = ctx->idx - 1;

    while (!g_stop) {
        // Simule arrivée client
        usleep(200 * 1000 + (rand_r(&ctx->seed) % 400) * 1000); // 200..600ms
        if (g_stop) break;

        if (sem_wait_intr(&g_freeSlots) != 0) break;
        if (g_stop) {
            sem_post(&g_freeSlots);
            break;
        }

        Commande c = make_commande(ctx);

        // push dans la file (section critique)
        pthread_mutex_lock(&g_mtx_queue);
        queue_push(&g_queue, c);
        pthread_mutex_unlock(&g_mtx_queue);

        sem_post(&g_items);

        // update dashboard
        pthread_mutex_lock(&g_mtx_status);
        g_srv[i].last_id = c.id;
        g_srv[i].last_plat = c.plat;
        g_srv[i].last_prep_ms = c.prep_ms;
        g_srv[i].ts_ms = now_ms();
        g_srv[i].produced_count++;
        g_total_produced++;
        pthread_mutex_unlock(&g_mtx_status);
    }

    return NULL;
}

static void* cuisinier_thread(void *arg) {
    ThreadCtx *ctx = (ThreadCtx*)arg;
    int i = ctx->idx - 1;

    for (;;) {
        if (sem_wait_intr(&g_items) != 0) break;

        // pop dans la file (section critique)
        pthread_mutex_lock(&g_mtx_queue);
        Commande c;
        int ok = queue_pop(&g_queue, &c);
        pthread_mutex_unlock(&g_mtx_queue);

        if (ok != 0) {
            if (g_stop) break;
            continue;
        }

        sem_post(&g_freeSlots);

        if (c.id == -1) {
            break; // poison pill => arrêt
        }

        long long start = now_ms();
        long long end = start + c.prep_ms;

        // update dashboard (début cuisson)
        pthread_mutex_lock(&g_mtx_status);
        g_ck[i].busy = 1;
        g_ck[i].current_id = c.id;
        g_ck[i].current_plat = c.plat;
        g_ck[i].prep_ms = c.prep_ms;
        g_ck[i].end_ms = end;
        pthread_mutex_unlock(&g_mtx_status);

        // cuisson (sleep)
        usleep((useconds_t)c.prep_ms * 1000);

        // update dashboard (fin cuisson)
        pthread_mutex_lock(&g_mtx_status);
        g_ck[i].busy = 0;
        g_ck[i].done_count++;
        g_total_done++;
        pthread_mutex_unlock(&g_mtx_status);
    }

    pthread_mutex_lock(&g_mtx_status);
    g_cooks_done_threads++;
    pthread_mutex_unlock(&g_mtx_status);

    return NULL;
}

static void render_dashboard(void) {
    size_t qsz;
    pthread_mutex_lock(&g_mtx_queue);
    qsz = g_queue.count;
    pthread_mutex_unlock(&g_mtx_queue);

    long long t = now_ms();

    pthread_mutex_lock(&g_mtx_status);

    // Clear écran + curseur en haut
    printf("\033[H\033[J");

    printf("=== RESTAURANT (threads) ===   file=%zu/%d   produites=%d   finies=%d   STOP=%d\n",
           qsz, QUEUE_CAP, g_total_produced, g_total_done, (int)g_stop);

    printf("\n-- Serveurs (producteurs) --\n");
    for (int i = 0; i < NB_SERVEURS; i++) {
        if (g_srv[i].last_id <= 0) {
            printf("Serveur %d : (aucune commande)\n", i + 1);
        } else {
            long long age = t - g_srv[i].ts_ms;
            printf("Serveur %d : +#%d %-6s %4dms  | total=%d | il y a %lldms\n",
                   i + 1, g_srv[i].last_id, plat_name(g_srv[i].last_plat),
                   g_srv[i].last_prep_ms, g_srv[i].produced_count, age);
        }
    }

    printf("\n-- Cuisiniers (consommateurs) --\n");
    for (int i = 0; i < NB_CUISINIERS; i++) {
        if (g_ck[i].busy) {
            long long rem = g_ck[i].end_ms - t;
            if (rem < 0) rem = 0;
            printf("Cuisinier %d : #%-4d %-6s  reste ~%lldms | finis=%d\n",
                   i + 1, g_ck[i].current_id, plat_name(g_ck[i].current_plat),
                   rem, g_ck[i].done_count);
        } else {
            printf("Cuisinier %d : (idle)                 | finis=%d\n",
                   i + 1, g_ck[i].done_count);
        }
    }

    printf("\nCtrl+C pour arrêter (le programme s'arrête après que la file en attente soit fini).\n");

    pthread_mutex_unlock(&g_mtx_status);
    fflush(stdout);
}

static void* dashboard_thread(void *arg) {
    (void)arg;

    // cache le curseur (optionnel, mais propre)
    printf("\033[?25l");
    fflush(stdout);

    while (1) {
        render_dashboard();

        pthread_mutex_lock(&g_mtx_status);
        int done = (g_stop && g_cooks_done_threads == NB_CUISINIERS);
        pthread_mutex_unlock(&g_mtx_status);

        if (done) break;
        usleep(200 * 1000); // refresh 200ms
    }

    // ré-affiche le curseur
    printf("\033[?25h");
    fflush(stdout);
    return NULL;
}

static void push_poison_pills(int n) {
    for (int i = 0; i < n; i++) {
        if (sem_wait_intr(&g_freeSlots) != 0) return;

        pthread_mutex_lock(&g_mtx_queue);
        Commande poison = { .id = -1, .plat = PLAT_PIZZA, .prep_ms = 0 };
        queue_push(&g_queue, poison);
        pthread_mutex_unlock(&g_mtx_queue);

        sem_post(&g_items);
    }
}

// =====================
// main
// =====================
int main(void) {
    install_sigint();

    if (queue_init(&g_queue, QUEUE_CAP) != 0) {
        fprintf(stderr, "Erreur: queue_init\n");
        return EXIT_FAILURE;
    }

    if (sem_init(&g_freeSlots, 0, QUEUE_CAP) != 0) {
        perror("sem_init freeSlots");
        return EXIT_FAILURE;
    }
    if (sem_init(&g_items, 0, 0) != 0) {
        perror("sem_init items");
        return EXIT_FAILURE;
    }

    // init status
    pthread_mutex_lock(&g_mtx_status);
    memset(g_srv, 0, sizeof(g_srv));
    memset(g_ck, 0, sizeof(g_ck));
    pthread_mutex_unlock(&g_mtx_status);

    pthread_t serveurs[NB_SERVEURS];
    pthread_t cuisiniers[NB_CUISINIERS];
    pthread_t dash;

    ThreadCtx sctx[NB_SERVEURS];
    ThreadCtx cctx[NB_CUISINIERS];

    unsigned base = (unsigned)time(NULL);

    for (int i = 0; i < NB_SERVEURS; i++) {
        sctx[i].idx = i + 1;
        sctx[i].seed = base ^ (unsigned)(0xA53u + i * 997u);
        pthread_create(&serveurs[i], NULL, serveur_thread, &sctx[i]);
    }

    for (int i = 0; i < NB_CUISINIERS; i++) {
        cctx[i].idx = i + 1;
        cctx[i].seed = base ^ (unsigned)(0xC11u + i * 733u);
        pthread_create(&cuisiniers[i], NULL, cuisinier_thread, &cctx[i]);
    }

    pthread_create(&dash, NULL, dashboard_thread, NULL);

    // attente stop
    while (!g_stop) usleep(100 * 1000);

    // stop: poison pills pour terminer les cuisiniers
    push_poison_pills(NB_CUISINIERS);

    for (int i = 0; i < NB_SERVEURS; i++) pthread_join(serveurs[i], NULL);
    for (int i = 0; i < NB_CUISINIERS; i++) pthread_join(cuisiniers[i], NULL);
    pthread_join(dash, NULL);

    sem_destroy(&g_freeSlots);
    sem_destroy(&g_items);
    pthread_mutex_destroy(&g_mtx_queue);
    pthread_mutex_destroy(&g_mtx_status);
    queue_destroy(&g_queue);

    // petit résumé final (lisible)
    printf("\nRésumé: produites=%d | finies=%d | capacité=%d | serveurs=%d | cuisiniers=%d\n",
           g_total_produced, g_total_done, QUEUE_CAP, NB_SERVEURS, NB_CUISINIERS);

    return EXIT_SUCCESS;
}
