#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>

#define PE_PRIO_NORMAL 4
#define PE_PRIO_HIGH 2

#define PE_NEWID ('e'+'v')  /* for New() macro */

static SV *DebugLevel, *Eval, *Stats;

#include "Event.h"

static void
queueEvent(pe_event *ev, int count);

#include "typemap.c"
#include "gettimeofday.c"  /* hack XXX */
#include "event_vtbl.c"
#include "idle.c"
#include "timer.c"
#include "io.c"
#include "unix_io.c"
#include "watchvar.c"
#include "signal.c"

/* TODO */
#include "process.c"

#include "queue.c"


MODULE = Event		PACKAGE = Event

PROTOTYPES: DISABLE

BOOT:
  DebugLevel = SvREFCNT_inc(perl_get_sv("Event::DebugLevel", 1));
  Eval = SvREFCNT_inc(perl_get_sv("Event::Eval", 1));
  Stats = SvREFCNT_inc(perl_get_sv("Event::Stats", 1));
  boot_pe_event();
  boot_idle();
  boot_timer();
  boot_io();
  boot_watchvar();
  boot_process();
  boot_signal();
  boot_queue();

void
DESTROY(ref)
	SV *ref
	CODE:
	SV *sv;
	if (!SvRV(ref))
	  croak("Expected RV");
	sv = SvRV(ref);
	/* will be called twice for each Event; yuk! */
	if (SvTYPE(sv) == SVt_PVMG) {
	  pe_event *THIS = (pe_event*) SvIV(sv);
	  /* warn("X sv(%s)=0x%x", SvPV(THIS->desc,na), THIS); /**/
	  --THIS->refcnt;
	  if (EvCANDESTROY(THIS) || (THIS->refcnt == 0 && PL_in_clean_objs)) {
	    (*THIS->vtbl->dtor)(THIS);
	  }
	}

void
pe_event::again()
	CODE:
	/* more natural if NO repeat */
	(*THIS->vtbl->start)(THIS, 0);

void
pe_event::start()
	CODE:
	/* more natural if repeat */
	(*THIS->vtbl->start)(THIS, 0);

void
pe_event::suspend()
	CODE:
	pe_event_suspend(THIS);

void
pe_event::cancel()
	CODE:
	pe_event_cancel(THIS);

void
pe_event::now()
	CODE:
	pe_event_now(THIS);

void
pe_event::FETCH(key)
	SV *key;
	PPCODE:
	PUTBACK;
	(*THIS->vtbl->FETCH)(THIS, key);
	SPAGAIN;

void
pe_event::STORE(key,nval)
	SV *key
	SV *nval
	PPCODE:
	PUTBACK;
	(*THIS->vtbl->STORE)(THIS, key, nval);
	SPAGAIN;

void
pe_event::DELETE(key)
	SV *key
	PPCODE:
	PUTBACK;
	(*THIS->vtbl->DELETE)(THIS, key);
	SPAGAIN;

void
pe_event::EXISTS(key)
	SV *key
	PPCODE:
	PUTBACK;
	(*THIS->vtbl->EXISTS)(THIS, key);
	SPAGAIN;

void
pe_event::FIRSTKEY()
	PPCODE:
	PUTBACK;
	(*THIS->vtbl->FIRSTKEY)(THIS);
	SPAGAIN;

void
pe_event::NEXTKEY(prevkey)
	SV *prevkey;
	PPCODE:
	PUTBACK;
	(*THIS->vtbl->NEXTKEY)(THIS);
	SPAGAIN;


MODULE = Event		PACKAGE = Event::Loop

double
null_loops_per_second(sec)
	int sec
	CODE:
	struct timeval start_tm, done_tm;
	double elapse;
	unsigned count=0;
	gettimeofday(&start_tm, 0);
	do {
	  /* This should be more realistic... XXX */
#ifdef HAS_POLL
	  struct pollfd junk;
	  poll(&junk, 0, 0);
#else
#ifdef HAS_SELECT
	  select(0,0,0,0,0);
#endif
#endif
	  ++count;
	  gettimeofday(&done_tm, 0);
	  elapse = (done_tm.tv_sec - start_tm.tv_sec +
		    (done_tm.tv_usec - start_tm.tv_usec) / 1000000);
	} while(elapse < sec);
	RETVAL = count/sec;
	OUTPUT:
	RETVAL

void
listQ()
	PPCODE:
	int xx;
	pe_event *ev;
	for (xx=0; xx < QUEUES; xx++) {
	  ev = Queue[xx].prev->self;
	  while (ev) {
	    XPUSHs(sv_2mortal(event_2sv(ev)));
	    ev = ev->que.prev->self;
	  }
	}
	ev = Idle.prev->self;
	while (ev) {
	  XPUSHs(sv_2mortal(event_2sv(ev)));
	  ev = ev->que.prev->self;
	}

int
runIdle()

int
wantIdle()

void
queueEvent(ev)
	pe_event *ev;
	CODE:
	queueEvent(ev, 1);

int
emptyQueue(...)
	PROTOTYPE: ;$
	CODE:
	int max = QUEUES;
	if (items == 1)
	  max = SvIV(ST(0));
	RETVAL = emptyQueue(max);
	OUTPUT:
	RETVAL

int
doOneEvent()


MODULE = Event		PACKAGE = Event::idle

pe_event *
allocate()
	CODE:
	RETVAL = pe_idle_allocate();
	OUTPUT:
	RETVAL


MODULE = Event		PACKAGE = Event::timer

void
List()
	PPCODE:
	int xx;
	pe_event *ev;
	ev = Timers.next->self;
	while (ev) {
	  XPUSHs(sv_2mortal(event_2sv(ev)));
	  ev = ev->que.next->self;
	}

void
checkTimers()

double
timeTillTimer()

pe_event *
allocate()
	CODE:
	RETVAL = pe_timer_allocate();
	OUTPUT:
	RETVAL


MODULE = Event		PACKAGE = Event::io

pe_event *
allocate()
	CODE:
	RETVAL = pe_io_allocate();
	OUTPUT:
	RETVAL

void
waitForEvent(timeout)
	double timeout;
	CODE:
	pe_io_waitForEvent(timeout);


MODULE = Event		PACKAGE = Event::watchvar

pe_event *
allocate()
	CODE:
	RETVAL = pe_watchvar_allocate();
	OUTPUT:
	RETVAL


MODULE = Event		PACKAGE = Event::signal

pe_event *
allocate()
	CODE:
	RETVAL = pe_signal_allocate();
	OUTPUT:
	RETVAL


MODULE = Event		PACKAGE = Event::process

int
_count(...)
PROTOTYPE:
CODE:
    RETVAL = chld_next;
OUTPUT:
    RETVAL

void
_reap()
PROTOTYPE:
PPCODE:
{
    int count = 0;
    if(chld_next) {
	int pid, status, slot;
	(void)rsignal(SIGCHLD, SIG_DFL);
	slot = chld_next;
	EXTEND(sp, (slot * 2));
	while( slot-- ) {
	    if(chld_buf[slot].pid > 0) {
		count += 2;
		XPUSHs(sv_2mortal(newSViv(chld_buf[slot].pid)));
		XPUSHs(sv_2mortal(newSViv(chld_buf[slot].status)));
	    }
	}
	chld_next = 0;
#ifdef Wait4Any
	while((pid = Wait4Any(&status)) > 0) {
	    EXTEND(sp,2);
	    count += 2;
	    XPUSHs(sv_2mortal(newSViv(pid)));
	    XPUSHs(sv_2mortal(newSViv(status)));
	}
#endif
	(void)rsignal(SIGCHLD, chld_sighandler);
    }
    XSRETURN(count);
}


