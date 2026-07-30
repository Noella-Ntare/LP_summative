/*
 * order_system.c
 *
 * Simulates an online food-delivery order pipeline using POSIX threads:
 *   - Kitchen thread   (producer): prepares an order every 2 seconds,
 *                                  places it in a fixed-size shared queue.
 *   - Delivery thread  (consumer): removes an order every 4 seconds,
 *                                  processes it for delivery.
 *   - Monitoring thread          : every 5 seconds, safely prints
 *                                  prepared/delivered counts and queue size.
 *
 * Synchronization: one mutex protects all shared state; two condition
 * variables ("not_full" and "not_empty") coordinate the producer and
 * consumer so the queue capacity (5) is never exceeded and items are
 * never removed from an empty queue.
 *
 * Build:  gcc -Wall -O2 -pthread -o order_system order_system.c
 * Run:    ./order_system            (default: processes 20 orders then exits)
 *         ./order_system 40         (optional: process a custom order count)
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <stdatomic.h>

#define QUEUE_CAPACITY 5

/* Shared queue state and counters protected by the mutex. */
typedef struct {
    int orders[QUEUE_CAPACITY];
    int count;          /* current number of orders waiting in the queue */
    int head;           /* index of the next order to remove */
    int tail;           /* index where the next order will be inserted   */

    int next_order_id;      /* incremental order ID generator */
    int orders_prepared;    /* running total, for monitoring   */
    int orders_delivered;   /* running total, for monitoring   */

    int total_orders_target;   /* stop condition: how many orders to run */
    int producer_done;         /* set to 1 once the kitchen has produced
                                   `total_orders_target` orders           */

    pthread_mutex_t lock;
    pthread_cond_t  not_full;   /* signalled when queue has space  */
    pthread_cond_t  not_empty;  /* signalled when queue has an item */
} order_queue_t;

static order_queue_t q;
/* atomic_int (not plain volatile int) so main-thread writes and the
 * monitor thread's reads are properly synchronized -- a plain volatile
 * flag is not sufficient across threads in C and was flagged as a data
 * race by ThreadSanitizer during testing. */
static atomic_int g_shutdown = 0; /* tells the monitor thread to stop */

/* Format the current time for readable thread output. */
static void print_timestamp(void) {
    time_t now = time(NULL);
    struct tm lt;
    /* localtime_r (reentrant/thread-safe variant) is used instead of
     * localtime(), because localtime() writes into a single shared
     * static buffer and is unsafe when called concurrently from
     * multiple threads -- confirmed as a real data race by
     * ThreadSanitizer during testing of this program. */
    localtime_r(&now, &lt);
    printf("[%02d:%02d:%02d] ", lt.tm_hour, lt.tm_min, lt.tm_sec);
}

/* =================================================================
 * Kitchen thread (producer)
 * ================================================================= */
void *kitchen_thread(void *arg) {
    (void)arg;
    while (1) {
        /* Simulate 2 seconds of food preparation *outside* the lock,
         * so the kitchen doesn't hold the mutex while "cooking". */
        sleep(2);

        int order_id;

        pthread_mutex_lock(&q.lock);

        /* Stop condition: enough orders already produced */
        if (q.next_order_id > q.total_orders_target) {
            q.producer_done = 1;
            pthread_cond_broadcast(&q.not_empty); /* wake consumer to let it exit */
            pthread_mutex_unlock(&q.lock);
            break;
        }

        /* Wait while the queue is full (bounded buffer). */
        while (q.count == QUEUE_CAPACITY) {
            pthread_cond_wait(&q.not_full, &q.lock);
        }

        order_id = q.next_order_id++;
        q.orders[q.tail] = order_id;
        q.tail = (q.tail + 1) % QUEUE_CAPACITY;
        q.count++;
        q.orders_prepared++;

        print_timestamp();
        printf("[KITCHEN]  Order #%d prepared. Queue size = %d\n",
               order_id, q.count);

        /* Wake the delivery thread: there's now an item to consume. */
        pthread_cond_signal(&q.not_empty);
        pthread_mutex_unlock(&q.lock);
    }
    return NULL;
}

/* =================================================================
 * Delivery thread (consumer)
 * ================================================================= */
void *delivery_thread(void *arg) {
    (void)arg;
    while (1) {
        int order_id;

        pthread_mutex_lock(&q.lock);

        /* Wait while the queue is empty, unless production has finished
         * and there is nothing left to deliver. */
        while (q.count == 0 && !q.producer_done) {
            pthread_cond_wait(&q.not_empty, &q.lock);
        }

        if (q.count == 0 && q.producer_done) {
            pthread_mutex_unlock(&q.lock);
            break; /* nothing left to ever deliver: exit cleanly */
        }

        order_id = q.orders[q.head];
        q.head = (q.head + 1) % QUEUE_CAPACITY;
        q.count--;

        print_timestamp();
        printf("[DELIVERY] Order #%d picked up. Queue size = %d\n",
               order_id, q.count);

        /* Wake the kitchen thread: there's now space available. */
        pthread_cond_signal(&q.not_full);
        pthread_mutex_unlock(&q.lock);

        /* Simulate 4 seconds of delivery *outside* the lock. */
        sleep(4);

        pthread_mutex_lock(&q.lock);
        q.orders_delivered++;
        print_timestamp();
        printf("[DELIVERY] Order #%d delivered.\n", order_id);
        pthread_mutex_unlock(&q.lock);
    }
    return NULL;
}

/* =================================================================
 * Monitoring thread
 * Reads shared counters/queue size under the same mutex used by the
 * producer/consumer, so it always observes a consistent snapshot,
 * but only ever reads -> it never blocks producer/consumer for long.
 * ================================================================= */
void *monitor_thread(void *arg) {
    (void)arg;
    while (!g_shutdown) {
        sleep(5);
        if (g_shutdown) break;

        pthread_mutex_lock(&q.lock);
        int prepared = q.orders_prepared;
        int delivered = q.orders_delivered;
        int size = q.count;
        pthread_mutex_unlock(&q.lock);

        print_timestamp();
        printf("[MONITOR]  Orders prepared: %d | Orders delivered: %d | "
               "Current queue size: %d\n", prepared, delivered, size);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    int target = 20;
    if (argc > 1) {
        target = atoi(argv[1]);
        if (target <= 0) target = 20;
    }

    /* Initialize shared queue state */
    q.count = 0;
    q.head = 0;
    q.tail = 0;
    q.next_order_id = 1;
    q.orders_prepared = 0;
    q.orders_delivered = 0;
    q.total_orders_target = target;
    q.producer_done = 0;
    pthread_mutex_init(&q.lock, NULL);
    pthread_cond_init(&q.not_full, NULL);
    pthread_cond_init(&q.not_empty, NULL);

    printf("Starting order processing system (target: %d orders, "
           "queue capacity: %d)\n\n", target, QUEUE_CAPACITY);

    pthread_t kitchen_tid, delivery_tid, monitor_tid;
    pthread_create(&kitchen_tid, NULL, kitchen_thread, NULL);
    pthread_create(&delivery_tid, NULL, delivery_thread, NULL);
    pthread_create(&monitor_tid, NULL, monitor_thread, NULL);

    /* Wait for the producer/consumer to finish all target orders. */
    pthread_join(kitchen_tid, NULL);
    pthread_join(delivery_tid, NULL);

    /* Signal the monitor thread to stop and wait briefly for it. */
    g_shutdown = 1;
    pthread_cancel(monitor_tid);
    pthread_join(monitor_tid, NULL);

    pthread_mutex_lock(&q.lock);
    printf("\nFinal totals -> prepared: %d, delivered: %d, "
           "remaining in queue: %d\n",
           q.orders_prepared, q.orders_delivered, q.count);
    pthread_mutex_unlock(&q.lock);

    pthread_mutex_destroy(&q.lock);
    pthread_cond_destroy(&q.not_full);
    pthread_cond_destroy(&q.not_empty);

    return 0;
}
