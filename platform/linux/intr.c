#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>

#include "platform.h"

#include "util.h"

struct irq_entry {
  struct irq_entry *next;
  unsigned int irq; /* IRQ (interrupt request) number */
  int (*handler)(unsigned int irq, void *dev);
  int flags;
  char name[16];
  void *dev;
};

static struct irq_enrty *irqs; /* IRQ list */

static sigset_t sigmask; /* signal set */

static pthread_t tid;
static pthread_barrier_t barrier;

int intr_request_irq(unsigned int irq, int (*handler)(unsigned int irq, void *dev), int flags, const char *name, void *dev) {
  struct irq_entry *entry;

  debugf("irq=%u, flags=%d, name=%s", irq, flags, name);
  for (entry = irqs; entry; entry = entry->next) {
    if (entry->irq == irq) {
      if (entry->flags ^ INTR_IRQ_SHARED || flags ^ INTR_IRQ_SHARED) {
        errorf("conflicts with already registered IRQs");
        return -1;
      }
    }
  }

  entry = memory_alloc(sizeof(*entry));
  if (!entry) {
    errorf("memory_alloc() failure");
    return -1;
  }

  /* setup values to IRQ struct */
  entry->irq = irq;
  entry->handler = handler;
  entry->flags = flags;
  strncpy(entry->name, name, sizeof(entry->name)-1);
  entry->dev = dev;

  /* add head of IRQ list */
  entry->next = irqs;
  irqs = entry;

  /* add new signal to signal set */
  sigaddset(&sigmask, irq);

  debugf("registered: irq=%u, name=%s,", irq, name);

  return 0;
}

int intr_raise_irq(unsigned int irq) {
  /* send signal to interrupt process thread */
  return pthread_kill(tid, (int)irq);
}

static void *intr_thread(void *arg) {
  int terminate = 0, sig, err;
  struct irq_entry *entry;

  debugf("start...");

  /* process for synchronizing with main thread */
  pthread_barrier_wait(&barrier);

  while (!terminate) {
    /* stan-by until interrupt signal dispatched */
    err = sigwait(&sigmask, &sig);
    if (err) {
      errorf("sigwait() %s", strerror(err));
      break;
    }

    switch(sig) {
      case SIGHUP: /* SIGHUP (signal hang up): signal for notification of terminated interrupt thread */
        terminate = 1;
        break;
      default:
        for (entry = irqs; entry; entry = entry->next) {
          if (entry->irq == (unsigned int)sig) { /* call interrupt handler corresponded with IRQ number */
            debugf("irq=%d, name=%s", entry->irq, entry->name);
            entry->handler(entry->irq, entry->dev);
          }
        }
        break;
    }
  }

  debugf("terminated");
  return NULL;
}

int intr_run(void) {
  int err;

  /* setup signal mask */
  err = pthread_sigmask(SIG_BLOCK, &sigmask, NULL);
  if (err) {
    errorf("pthread_sigmask() %s", strerror(err));
    return -1;
  }

  /* run interrupt thread */
  err = pthread_create(&tid, NULL, intr_thread, NULL);
  if (err) {
    errorf("pthread_create() %s", strerror(err));
    return -1;
  }

  /* wait until thread is running */
  pthread_barrier_wait(&barrier);

  return 0;
}

void intr_shutdown(void) {
  if (pthread_equal(tid, pthread_self()) != 0) {
    /* Thread not created */
    return;
  }

  /* send SIGHUP to interrupt process thread */
  pthread_kill(tid, SIGHUP);
  /* wait for process terminated */
  pthread_join(tid, NULL);
}

int intr_init(void) {
  /* setup main thread id as initial thread id */
  tid = pthread_self();
  /* initialize pthread_barrier (count is 2)*/
  pthread_barrier_init(&barrier, NULL, 2);
  /* initialize signal set (as empty set) */
  sigemptyset(&sigmask);
  /* add SIGHUP to signal set (for notifications of terminating interrupt thread) */
  sigaddset(&sigmask, SIGHUP);

  return 0;
}
