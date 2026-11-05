/**
 * @file test_queue.c
 * @brief Multithreaded concurrency test for MiniDrop synchronization event queue.
 */

#include "queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>
#include <unistd.h>

#define NUM_PRODUCERS 4
#define NUM_CONSUMERS 2
#define ITEMS_PER_PRODUCER 250

static sync_queue_t g_test_queue;
static int g_total_consumed = 0;
static pthread_mutex_t g_count_mutex = PTHREAD_MUTEX_INITIALIZER;

void *producer_thread(void *arg) {
    int id = *(int *)arg;
    for (int i = 0; i < ITEMS_PER_PRODUCER; i++) {
        sync_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = SYNC_EV_CREATE;
        snprintf(ev.rel_path, sizeof(ev.rel_path), "prod_%d_file_%d.txt", id, i);
        ev.timestamp = (uint64_t)i;

        int res = sync_queue_push(&g_test_queue, &ev);
        assert(res == 0);
    }
    return NULL;
}

void *consumer_thread(void *arg) {
    (void)arg;
    sync_event_t ev;
    while (1) {
        int res = sync_queue_pop(&g_test_queue, &ev);
        if (res == 1) {
            /* Queue shut down cleanly */
            break;
        }
        assert(res == 0);

        pthread_mutex_lock(&g_count_mutex);
        g_total_consumed++;
        pthread_mutex_unlock(&g_count_mutex);
    }
    return NULL;
}

int main(void) {
    printf("=========================================\n");
    printf("   MiniDrop Queue Concurrency Test       \n");
    printf("=========================================\n");

    int res = sync_queue_init(&g_test_queue, 64);
    assert(res == 0);

    pthread_t consumers[NUM_CONSUMERS];
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        pthread_create(&consumers[i], NULL, consumer_thread, NULL);
    }

    pthread_t producers[NUM_PRODUCERS];
    int prod_ids[NUM_PRODUCERS];
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        prod_ids[i] = i;
        pthread_create(&producers[i], NULL, producer_thread, &prod_ids[i]);
    }

    for (int i = 0; i < NUM_PRODUCERS; i++) {
        pthread_join(producers[i], NULL);
    }
    printf("  [PASS] All %d producers finished enqueuing %d total items\n",
           NUM_PRODUCERS, NUM_PRODUCERS * ITEMS_PER_PRODUCER);

    /* Signal queue shutdown after producers finish */
    sync_queue_shutdown(&g_test_queue);

    for (int i = 0; i < NUM_CONSUMERS; i++) {
        pthread_join(consumers[i], NULL);
    }
    printf("  [PASS] All %d consumers finished draining queue cleanly\n", NUM_CONSUMERS);

    printf("  [INFO] Total consumed items: %d (Expected: %d)\n",
           g_total_consumed, NUM_PRODUCERS * ITEMS_PER_PRODUCER);
    assert(g_total_consumed == NUM_PRODUCERS * ITEMS_PER_PRODUCER);

    sync_queue_destroy(&g_test_queue);

    printf("-----------------------------------------\n");
    printf("Queue Concurrency: ALL ASSERTIONS PASSED!\n");
    printf("=========================================\n");

    return EXIT_SUCCESS;
}

