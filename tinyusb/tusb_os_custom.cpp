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

#include "miosix.h"
#include "tusb.h"

extern "C" {

void osal_task_delay(uint32_t msec)
{
    miosix::Thread::sleep(msec);
}

osal_semaphore_t osal_semaphore_create(osal_semaphore_def_t *semdef)
{
    semdef->handle = new miosix::Semaphore();
    return semdef;
}

bool osal_semaphore_post(osal_semaphore_t sem_hdl, bool in_isr)
{
    miosix::Semaphore *sem = static_cast<miosix::Semaphore*>(sem_hdl->handle);
    if(in_isr) sem->IRQsignal();
    else sem->signal();
    return true;
}

bool osal_semaphore_wait(osal_semaphore_t sem_hdl, uint32_t msec)
{
    miosix::Semaphore *sem = static_cast<miosix::Semaphore*>(sem_hdl->handle);
    if(msec == OSAL_TIMEOUT_WAIT_FOREVER) sem->wait();
    else sem->timedWait(miosix::getTime() + (long long)msec * 1000000LL);
    return true;
}

void osal_semaphore_reset(osal_semaphore_t sem_hdl)
{
    (void) sem_hdl;
}

osal_queue_t osal_queue_create(osal_queue_def_t *qdef)
{
    tu_fifo_config(&qdef->fifo, qdef->buf, qdef->depth, qdef->item_size, false);
    tu_fifo_clear(&qdef->fifo);
    qdef->sem = new miosix::Semaphore();
    return qdef;
}

void osal_queue_delete(osal_queue_t qhdl)
{
    (void) qhdl;
}

bool osal_queue_receive(osal_queue_t qhdl, void *data, uint32_t msec)
{
    miosix::Semaphore *sem = static_cast<miosix::Semaphore*>(qhdl->sem);
    if(msec == OSAL_TIMEOUT_WAIT_FOREVER) sem->wait();
    else sem->timedWait(miosix::getTime() + (long long)msec * 1000000LL);
    bool success;
    {
        miosix::FastGlobalIrqLock dLock;
        success = tu_fifo_read(&qhdl->fifo, data);
    }
    return success;
}

bool osal_queue_send(osal_queue_t qhdl, void const *data, bool in_isr)
{
    miosix::Semaphore *sem = static_cast<miosix::Semaphore*>(qhdl->sem);
    bool success;
    if(in_isr)
    {
        miosix::FastGlobalLockFromIrq dLock;
        success = tu_fifo_write(&qhdl->fifo, data);
        sem->IRQsignal();
    } else {
        {
            miosix::FastGlobalIrqLock dLock;
            success = tu_fifo_write(&qhdl->fifo, data);
        }
        sem->signal();
    }
    return success;
}

bool osal_queue_empty(osal_queue_t qhdl)
{
    return tu_fifo_empty(&qhdl->fifo);
}

}
