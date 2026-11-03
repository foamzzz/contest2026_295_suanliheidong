/*
 * Copyright (C) 2026 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * This file contains code derived from MimiClaw (https://github.com/memovai/mimiclaw)
 * Copyright (c) 2026 Ziboyan Wang, licensed under the MIT License.
 * See NOTICE file for the original MIT License terms.
 */

#include "core/message_bus.h"
#include "agent_config.h"
#include "agent_compat.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

static const char *TAG = "bus";

/* -- Private Types -------------------------------------------------------- */

typedef struct msg_overflow_node_s {
    agent_msg_t msg;
    struct msg_overflow_node_s *next;
} msg_overflow_node_t;

typedef struct {
    agent_msg_t  items[AGENT_BUS_QUEUE_LEN];
    /* Preserve FIFO without blocking network receive tasks when the main
     * ring is full.  Nodes are allocated only while backlog exists, so the
     * queue capacity does not consume another static DRAM block at boot. */
    msg_overflow_node_t *overflow_head;
    msg_overflow_node_t *overflow_tail;
    int             head;
    int             tail;
    int             count;
    int             overflow_count;
    pthread_mutex_t lock;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
} msg_queue_t;

/* -- Private Data --------------------------------------------------------- */

static msg_queue_t g_inbound_queue;
static msg_queue_t g_outbound_queue;
static bool g_initialized = false;
static volatile bool g_bus_shutdown = false;

/* -- Private Functions ---------------------------------------------------- */

static void msg_queue_init(msg_queue_t *queue)
{
    memset(queue, 0, sizeof(*queue));
    pthread_mutex_init(&queue->lock, NULL);
    pthread_cond_init(&queue->not_empty, NULL);
    pthread_cond_init(&queue->not_full, NULL);
}

static void msg_queue_destroy(msg_queue_t *queue)
{
    int i;
    msg_overflow_node_t *node;

    /* Release messages that are still waiting when the Agent is stopped. */
    for (i = 0; i < queue->count; i++) {
        int index = (queue->head + i) % AGENT_BUS_QUEUE_LEN;
        message_bus_msg_free(&queue->items[index]);
    }
    node = queue->overflow_head;
    while (node != NULL) {
        msg_overflow_node_t *next = node->next;
        message_bus_msg_free(&node->msg);
        free(node);
        node = next;
    }

    pthread_mutex_destroy(&queue->lock);
    pthread_cond_destroy(&queue->not_empty);
    pthread_cond_destroy(&queue->not_full);
    memset(queue, 0, sizeof(*queue));
}

static int msg_queue_push(msg_queue_t *queue, const agent_msg_t *msg,
                          uint32_t timeout_ms)
{
    struct timespec ts;

    pthread_mutex_lock(&queue->lock);

    while (queue->count >= AGENT_BUS_QUEUE_LEN &&
           queue->overflow_count >= AGENT_BUS_OVERFLOW_LEN) {
        if (timeout_ms == 0) {
            pthread_mutex_unlock(&queue->lock);
            return ERROR;
        }

        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += (time_t)(timeout_ms / 1000);
        ts.tv_nsec += (long)((timeout_ms % 1000) * 1000000L);
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }

        /* Wait for space */
        while (queue->count >= AGENT_BUS_QUEUE_LEN &&
               queue->overflow_count >= AGENT_BUS_OVERFLOW_LEN) {
            if (pthread_cond_timedwait(&queue->not_full, &queue->lock, &ts) != 0) {
                pthread_mutex_unlock(&queue->lock);
                syslog(LOG_WARNING, "[%s] Queue full, dropping message\n", TAG);
                return ERROR;
            }
        }
    }

    /* Once overflow is active, append all later messages there so that a
     * newly freed main-ring slot cannot reorder the backlog. */
    if (queue->overflow_count > 0 || queue->count >= AGENT_BUS_QUEUE_LEN) {
        msg_overflow_node_t *node = calloc(1, sizeof(*node));
        if (node == NULL) {
            pthread_mutex_unlock(&queue->lock);
            return ERROR;
        }
        node->msg = *msg;
        if (queue->overflow_tail != NULL) {
            queue->overflow_tail->next = node;
        } else {
            queue->overflow_head = node;
        }
        queue->overflow_tail = node;
        queue->overflow_count++;
    } else {
        queue->items[queue->tail] = *msg;
        queue->tail = (queue->tail + 1) % AGENT_BUS_QUEUE_LEN;
        queue->count++;
    }

    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->lock);
    return OK;
}

static int msg_queue_pop(msg_queue_t *queue, agent_msg_t *msg,
                         uint32_t timeout_ms)
{
    struct timespec ts;

    pthread_mutex_lock(&queue->lock);

    while (queue->count == 0 && queue->overflow_count == 0) {
        if (g_bus_shutdown) {
            pthread_mutex_unlock(&queue->lock);
            return ERROR;
        }
        if (timeout_ms == UINT32_MAX) {
            pthread_cond_wait(&queue->not_empty, &queue->lock);
        } else {
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += (time_t)(timeout_ms / 1000);
            ts.tv_nsec += (long)((timeout_ms % 1000) * 1000000L);
            if (ts.tv_nsec >= 1000000000L) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000L;
            }

            if (pthread_cond_timedwait(&queue->not_empty, &queue->lock, &ts) != 0) {
                pthread_mutex_unlock(&queue->lock);
                return ERROR;
            }
        }
    }

    if (queue->count > 0) {
        *msg = queue->items[queue->head];
        queue->head = (queue->head + 1) % AGENT_BUS_QUEUE_LEN;
        queue->count--;
    } else {
        msg_overflow_node_t *node = queue->overflow_head;
        *msg = node->msg;
        queue->overflow_head = node->next;
        if (queue->overflow_head == NULL) {
            queue->overflow_tail = NULL;
        }
        queue->overflow_count--;
        free(node);
    }

    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->lock);
    return OK;
}

/* -- Public Functions ----------------------------------------------------- */

void message_bus_msg_free(agent_msg_t *msg)
{
    if (msg == NULL) {
        return;
    }

    if (msg->content != NULL) {
        free(msg->content);
        msg->content = NULL;
    }

    if (msg->image_b64 != NULL) {
        free(msg->image_b64);
        msg->image_b64 = NULL;
    }
}

int message_bus_init(void)
{
    if (g_initialized) {
        return OK;
    }

    msg_queue_init(&g_inbound_queue);
    msg_queue_init(&g_outbound_queue);
    g_bus_shutdown = false;
    g_initialized = true;

    syslog(LOG_INFO, "[%s] Message bus initialized (depth %d + overflow %d)\n",
           TAG, AGENT_BUS_QUEUE_LEN, AGENT_BUS_OVERFLOW_LEN);
    return OK;
}

void message_bus_destroy(void)
{
    if (!g_initialized) {
        return;
    }

    msg_queue_destroy(&g_inbound_queue);
    msg_queue_destroy(&g_outbound_queue);
    g_initialized = false;

    syslog(LOG_INFO, "[%s] Message bus destroyed\n", TAG);
}

void message_bus_wakeup(void)
{
    g_bus_shutdown = true;

    pthread_mutex_lock(&g_inbound_queue.lock);
    pthread_cond_broadcast(&g_inbound_queue.not_empty);
    pthread_mutex_unlock(&g_inbound_queue.lock);

    pthread_mutex_lock(&g_outbound_queue.lock);
    pthread_cond_broadcast(&g_outbound_queue.not_empty);
    pthread_mutex_unlock(&g_outbound_queue.lock);
}

int message_bus_push_inbound(const agent_msg_t *msg)
{
    uint32_t timeout_ms;

    if (msg == NULL) {
        return ERROR;
    }

    /* Never stall the Feishu WebSocket receive task while Agent is busy.
     * Messages still enter the bounded queue; only an exhausted queue fails. */
    timeout_ms = strcmp(msg->channel, AGENT_CHAN_FEISHU) == 0
        ? 0 : AGENT_BUS_PUSH_TIMEOUT_MS;
    return msg_queue_push(&g_inbound_queue, msg, timeout_ms);
}

int message_bus_pop_inbound(agent_msg_t *msg, uint32_t timeout_ms)
{
    if (msg == NULL) {
        return ERROR;
    }

    return msg_queue_pop(&g_inbound_queue, msg, timeout_ms);
}

int message_bus_push_outbound(const agent_msg_t *msg)
{
    if (msg == NULL) {
        return ERROR;
    }

    return msg_queue_push(&g_outbound_queue, msg, AGENT_BUS_PUSH_TIMEOUT_MS);
}

int message_bus_pop_outbound(agent_msg_t *msg, uint32_t timeout_ms)
{
    if (msg == NULL) {
        return ERROR;
    }

    return msg_queue_pop(&g_outbound_queue, msg, timeout_ms);
}
