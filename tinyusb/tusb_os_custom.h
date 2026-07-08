/***************************************************************************
 *   Copyright (c) 2023 by Daniele Cattaneo                                *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   As a special exception, if other files instantiate templates or use   *
 *   macros or inline functions from this file, or you compile this file   *
 *   and link it with other works to produce a work based on this file,    *
 *   this file does not by itself cause the resulting work to be covered   *
 *   by the GNU General Public License. However the source code for this   *
 *   file must still be made available in accordance with the GNU General  *
 *   Public License. This exception does not invalidate any other reasons  *
 *   why a work based on this file might be covered by the GNU General     *
 *   Public License.                                                       *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, see <http://www.gnu.org/licenses/>   *
 ***************************************************************************/

#ifndef OSAL_MIOSIX_H
#define OSAL_MIOSIX_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

void osal_task_delay(uint32_t msec);

typedef struct {
    void (*interrupt_set)(bool enabled);
    uint32_t nested_count;
} osal_spinlock_t;

#define OSAL_SPINLOCK_DEF(_name, _int_set) \
    osal_spinlock_t _name = { .interrupt_set = _int_set, .nested_count = 0 }

static inline void osal_spin_init(osal_spinlock_t *ctx)
{
    (void) ctx;
}

static inline void osal_spin_lock(osal_spinlock_t *ctx, bool in_isr)
{
    if(!in_isr && ctx->nested_count == 0) ctx->interrupt_set(false);
    ctx->nested_count++;
}

static inline void osal_spin_unlock(osal_spinlock_t *ctx, bool in_isr)
{
    if(ctx->nested_count == 0) return;
    ctx->nested_count--;
    if(!in_isr && ctx->nested_count == 0) ctx->interrupt_set(true);
}

typedef struct { void *handle; } osal_semaphore_def_t;
typedef osal_semaphore_def_t *osal_semaphore_t;

osal_semaphore_t osal_semaphore_create(osal_semaphore_def_t *semdef);
bool osal_semaphore_post(osal_semaphore_t sem_hdl, bool in_isr);
bool osal_semaphore_wait(osal_semaphore_t sem_hdl, uint32_t msec);
void osal_semaphore_reset(osal_semaphore_t sem_hdl);

typedef pthread_mutex_t osal_mutex_def_t;
typedef pthread_mutex_t *osal_mutex_t;

static inline osal_mutex_t osal_mutex_create(osal_mutex_def_t *mdef)
{
    pthread_mutex_init(mdef, NULL);
    return mdef;
}

static inline void osal_mutex_delete(osal_mutex_t mutex_hdl)
{
    pthread_mutex_destroy(mutex_hdl);
}

static inline bool osal_mutex_lock(osal_mutex_t mutex_hdl, uint32_t msec)
{
    (void) msec;
    return pthread_mutex_lock(mutex_hdl) == 0;
}

static inline bool osal_mutex_unlock(osal_mutex_t mutex_hdl)
{
    return pthread_mutex_unlock(mutex_hdl) == 0;
}

#ifdef __cplusplus
}
#endif

#include "common/tusb_fifo.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    tu_fifo_t fifo;
    uint8_t *buf;
    uint16_t depth;
    uint16_t item_size;
    void *sem;
} osal_queue_def_t;
typedef osal_queue_def_t *osal_queue_t;

#define OSAL_QUEUE_DEF(_int_set, _name, _depth, _type) \
    uint8_t _name##_buf[_depth * sizeof(_type)]; \
    osal_queue_def_t _name = { .buf = _name##_buf, .depth = _depth, .item_size = sizeof(_type) }

osal_queue_t osal_queue_create(osal_queue_def_t *qdef);
void osal_queue_delete(osal_queue_t qhdl);
bool osal_queue_receive(osal_queue_t qhdl, void *data, uint32_t msec);
bool osal_queue_send(osal_queue_t qhdl, void const *data, bool in_isr);
bool osal_queue_empty(osal_queue_t qhdl);

#ifdef __cplusplus
}
#endif

#endif
