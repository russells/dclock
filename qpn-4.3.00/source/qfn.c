/*****************************************************************************
* Product: QF-nano implemenation
* Last Updated for Version: 4.3.00
* Date of the Last Update:  Oct 29, 2011
*
*                    Q u a n t u m     L e a P s
*                    ---------------------------
*                    innovating embedded systems
*
* Copyright (C) 2002-2011 Quantum Leaps, LLC. All rights reserved.
*
* This software may be distributed and modified under the terms of the GNU
* General Public License version 2 (GPL) as published by the Free Software
* Foundation and appearing in the file GPL.TXT included in the packaging of
* this file. Please note that GPL Section 2[b] requires that all works based
* on this software must also be made publicly available under the terms of
* the GPL ("Copyleft").
*
* Alternatively, this software may be distributed and modified under the
* terms of Quantum Leaps commercial licenses, which expressly supersede
* the GPL and are specifically designed for licensees interested in
* retaining the proprietary status of their code.
*
* Contact information:
* Quantum Leaps Web site:  http://www.quantum-leaps.com
* e-mail:                  info@quantum-leaps.com
*****************************************************************************/
#include "qpn_port.h"                                       /* QP-nano port */

Q_DEFINE_THIS_MODULE(qfn)

/**
* \file
* \ingroup qfn
* QF-nano implementation.
*/

/* Global-scope objects ----------------------------------------------------*/
uint8_t volatile QF_readySet_;                      /* ready-set of QF-nano */

/* local objects -----------------------------------------------------------*/
static uint8_t const Q_ROM Q_ROM_VAR l_pow2Lkup[] = {
    0x00U, 0x01U, 0x02U, 0x04U, 0x08U, 0x10U, 0x20U, 0x40U, 0x80U
};

/*..........................................................................*/
#if (Q_PARAM_SIZE != 0)
void QActive_post(QActive *me, QSignal sig, QParam par)
#else
void QActive_post(QActive *me, QSignal sig)
#endif
{
    QActiveCB const Q_ROM *ao = &QF_active[me->prio];

            /* the queue must be able to accept the event (cannot overflow) */
    Q_ASSERT(me->nUsed < Q_ROM_BYTE(ao->end));

    QF_INT_LOCK();
                                /* insert event into the ring buffer (FIFO) */
    ((QEvent *)Q_ROM_PTR(ao->queue))[me->head].sig = sig;
#if (Q_PARAM_SIZE != 0)
    ((QEvent *)Q_ROM_PTR(ao->queue))[me->head].par = par;
#endif
    if (me->head == (uint8_t)0) {
        me->head = Q_ROM_BYTE(ao->end);                    /* wrap the head */
    }
    --me->head;
    ++me->nUsed;
    if (me->nUsed == (uint8_t)1) {              /* is this the first event? */
        QF_readySet_ |= Q_ROM_BYTE(l_pow2Lkup[me->prio]);    /* set the bit */
#ifdef QK_PREEMPTIVE
        sig = QK_schedPrio_();          /* reuse 'sig' to hold the priority */
        if (sig != 0) {
            QK_sched_(sig);             /* check for synchronous preemption */
        }
#endif
    }
    QF_INT_UNLOCK();
}
/*..........................................................................*/
#if (Q_PARAM_SIZE != 0)
void QActive_postISR(QActive *me, QSignal sig, QParam par)
#else
void QActive_postISR(QActive *me, QSignal sig)
#endif
{
#ifdef QF_ISR_NEST
#ifdef QF_ISR_KEY_TYPE
    QF_ISR_KEY_TYPE key;
#endif
#endif
    QActiveCB const Q_ROM *ao = &QF_active[me->prio];

            /* the queue must be able to accept the event (cannot overflow) */
    Q_ASSERT(me->nUsed < Q_ROM_BYTE(ao->end));

#ifdef QF_ISR_NEST
#ifdef QF_ISR_KEY_TYPE
    QF_ISR_LOCK(key);
#else
    QF_INT_LOCK();
#endif
#endif
                                /* insert event into the ring buffer (FIFO) */
    ((QEvent *)Q_ROM_PTR(ao->queue))[me->head].sig = sig;
#if (Q_PARAM_SIZE != 0)
    ((QEvent *)Q_ROM_PTR(ao->queue))[me->head].par = par;
#endif
    if (me->head == (uint8_t)0) {
        me->head = Q_ROM_BYTE(ao->end);                    /* wrap the head */
    }
    --me->head;
    ++me->nUsed;
    if (me->nUsed == (uint8_t)1) {              /* is this the first event? */
        QF_readySet_ |= Q_ROM_BYTE(l_pow2Lkup[me->prio]);    /* set the bit */
    }

#ifdef QF_ISR_NEST
#ifdef QF_ISR_KEY_TYPE
    QF_ISR_UNLOCK(key);
#else
    QF_INT_UNLOCK();
#endif
#endif
}

/*--------------------------------------------------------------------------*/
#if (QF_TIMEEVT_CTR_SIZE != 0)

/*..........................................................................*/
void QF_tickISR(void) {
    uint8_t p = (uint8_t)QF_MAX_ACTIVE;
    QActiveCB const Q_ROM *ao = &QF_active[(uint8_t)QF_MAX_ACTIVE];
    do {
        QActive *a = (QActive *)Q_ROM_PTR(ao->act);
        if (a->tickCtr != (QTimeEvtCtr)0) {
            --a->tickCtr;
            if (a->tickCtr == (QTimeEvtCtr)0) {
#if (Q_PARAM_SIZE != 0)
    QActiveCB const Q_ROM *ao = &QF_active[a->prio];
    Q_ASSERT(a->nUsed < Q_ROM_BYTE(ao->end));
                QActive_postISR(a, (QSignal)Q_TIMEOUT_SIG, (QParam)0);
#else
                QActive_postISR(a, (QSignal)Q_TIMEOUT_SIG);
#endif
            }
        }
        --ao;
        --p;
    } while (p != (uint8_t)0);
}

#if (QF_TIMEEVT_CTR_SIZE > 1)
/*..........................................................................*/
void QActive_arm(QActive *me, QTimeEvtCtr tout) {
    QF_INT_LOCK();
    me->tickCtr = tout;
    QF_INT_UNLOCK();
}
/*..........................................................................*/
void QActive_disarm(QActive *me) {
    QF_INT_LOCK();
    me->tickCtr = (QTimeEvtCtr)0;
    QF_INT_UNLOCK();
}
#endif                                     /* #if (QF_TIMEEVT_CTR_SIZE > 1) */

#endif                                    /* #if (QF_TIMEEVT_CTR_SIZE != 0) */

/*--------------------------------------------------------------------------*/
#ifndef QK_PREEMPTIVE

void QF_run(void) {
    static uint8_t const Q_ROM Q_ROM_VAR log2Lkup[] = {
        0U, 1U, 2U, 2U, 3U, 3U, 3U, 3U, 4U, 4U, 4U, 4U, 4U, 4U, 4U, 4U
    };
    static uint8_t const Q_ROM Q_ROM_VAR invPow2Lkup[] = {
        0xFFU, 0xFEU, 0xFDU, 0xFBU, 0xF7U, 0xEFU, 0xDFU, 0xBFU, 0x7FU
    };
    uint8_t p;
    QActive *a;
    QActiveCB const Q_ROM *ao;

                         /* set priorities all registered active objects... */
    for (p = (uint8_t)1; p <= (uint8_t)QF_MAX_ACTIVE; ++p) {
        a = (QActive *)Q_ROM_PTR(QF_active[p].act);
        Q_ASSERT(a != (QActive *)0);    /* QF_active[p] must be initialized */
        a->prio = p;               /* set the priority of the active object */
    }
         /* trigger initial transitions in all registered active objects... */
    for (p = (uint8_t)1; p <= (uint8_t)QF_MAX_ACTIVE; ++p) {
        a = (QActive *)Q_ROM_PTR(QF_active[p].act);
#ifndef QF_FSM_ACTIVE
        QHsm_init((QHsm *)a);         /* take the initial transition in HSM */
#else
        QFsm_init((QFsm *)a);         /* take the initial transition in FSM */
#endif
    }

    QF_onStartup();                              /* invoke startup callback */

    for (;;) {                      /* the event loop of the vanilla kernel */
        QF_INT_LOCK();
        if (QF_readySet_ != (uint8_t)0) {
#if (QF_MAX_ACTIVE > 4)
            if ((QF_readySet_ & 0xF0) != 0U) {        /* upper nibble used? */
                p = (uint8_t)(Q_ROM_BYTE(log2Lkup[QF_readySet_ >> 4]) + 4);
            }
            else                    /* upper nibble of QF_readySet_ is zero */
#endif
            {
                p = Q_ROM_BYTE(log2Lkup[QF_readySet_]);
            }
            ao = &QF_active[p];
            a = (QActive *)Q_ROM_PTR(ao->act);
            Q_ASSERT(a->nUsed > 0);        /* some events must be available */
            --a->nUsed;
            if (a->nUsed == (uint8_t)0) {          /* queue becoming empty? */
                QF_readySet_ &= Q_ROM_BYTE(invPow2Lkup[p]);/* clear the bit */
            }
            Q_SIG(a) =
                ((QEvent *)Q_ROM_PTR(ao->queue))[a->tail].sig;
#if (Q_PARAM_SIZE != 0)
            Q_PAR(a) =
                ((QEvent *)Q_ROM_PTR(ao->queue))[a->tail].par;
#endif
            if (a->tail == (uint8_t)0) {                    /* wrap around? */
                a->tail = Q_ROM_BYTE(ao->end);
            }
            --a->tail;
            QF_INT_UNLOCK();

#ifndef QF_FSM_ACTIVE
            QHsm_dispatch((QHsm *)a);                    /* dispatch to HSM */
#else
            QFsm_dispatch((QFsm *)a);                    /* dispatch to FSM */
#endif
        }
        else {
            QF_onIdle();                                      /* see NOTE01 */
        }
    }
}

#endif                                             /* #ifndef QK_PREEMPTIVE */

/*****************************************************************************
* NOTE01:
* QF_onIdle() must be called with interrupts locked because the determination
* of the idle condition (no events in the queues) can be changed by any
* interrupt. The QF_onIdle() MUST enable interrups internally, ideally
* atomically with putting the CPU into a low-power mode.
*/
