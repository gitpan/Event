#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>

#define PE_PRIO_NORMAL 4
#define PE_PRIO_HIGH 2

#define PE_NEWID ('e'+'v')  /* for New() macro */

static int Stats=0;
static SV *DebugLevel, *Eval;

#include "Event.h"

#if defined(HAS_POLL)
# include <poll.h>

# ifndef POLLRDNORM
# define POLLRDNORM POLLIN
# endif

# ifndef POLLRDBAND
# define POLLRDBAND POLLIN
# endif

# ifndef POLLWRBAND
# define POLLWRBAND POLLOUT
# endif
#endif

static void queueEvent(pe_event *ev, int count);
static void dequeEvent(pe_event *ev);

#include "typemap.c"
#include "gettimeofday.c"  /* hack XXX */
#include "event_vtbl.c"
#include "idle.c"
#include "timer.c"
#include "io.c"
#include "unix_io.c"
#include "watchvar.c"
#include "signal.c"
#include "tied.c"
#include "queue.c"
#include "stats.c"


MODULE = Event		PACKAGE = Event

PROTOTYPES: DISABLE

BOOT:
  DebugLevel = SvREFCNT_inc(perl_get_sv("Event::DebugLevel", 1));
  Eval = SvREFCNT_inc(perl_get_sv("Event::Eval", 1));
  boot_pe_event();
  boot_idle();
  boot_timer();
  boot_io();
  boot_watchvar();
  boot_tied();
  boot_signal();
  boot_queue();
  boot_stats();

void
DESTROY(ref)
	SV *ref
	CODE:
	SV *sv;
	if (!SvRV(ref))
	  croak("Expected RV");
	sv = SvRV(ref);
	/* warn("DESTROY %x", ref);/**/
	/* will be called twice for each Event; yuk! */
	if (SvTYPE(sv) == SVt_PVMG) {
	  pe_event *THIS = (pe_event*) SvIV(sv);
	  --THIS->refcnt;
	  /*warn("id=%d --refcnt=%d flags=0x%x",
		 THIS->id, THIS->refcnt,THIS->flags); /**/
	  if (EvCANDESTROY(THIS) || (THIS->refcnt == 0 && PL_in_clean_objs)) {
	    (*THIS->vtbl->dtor)(THIS);
	  }
	}
	/* else {
	  MAGIC *mg = mg_find(sv, 'P');
	  if (mg && SvREFCNT(SvRV(mg->mg_obj)) > 1)
	    warn("Event untie %d (debug)", SvREFCNT(SvRV(mg->mg_obj)) - 1);
	  sv_unmagic(sv, 'P');
	} */

void
pe_event::again()
	CODE:
	(*THIS->vtbl->start)(THIS, 1);

void
pe_event::start()
	CODE:
	(*THIS->vtbl->start)(THIS, 0);

void
pe_event::suspend()
	CODE:
	pe_event_suspend(THIS);

void
pe_event::resume()
	CODE:
	pe_event_resume(THIS);

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

void
pe_event::stats(sec)
	int sec
	PREINIT:
	int ran;
	double elapse;
	PPCODE:
	pe_stat_query(&THIS->stats, sec, &ran, &elapse);
	XPUSHs(sv_2mortal(newSViv(ran)));
	XPUSHs(sv_2mortal(newSVnv(elapse)));

pe_event *
allocate(class)
	SV *class
	CODE:
	RETVAL = pe_tied_allocate(class);
	OUTPUT:
	RETVAL

MODULE = Event		PACKAGE = Event::Stats

void
idle(class, sec)
	SV *class;
	int sec
	PREINIT:
	int ran;
	double elapse;
	PPCODE:
	pe_stat_query(&idleStats, sec, &ran, &elapse);
	XPUSHs(sv_2mortal(newSViv(ran)));
	XPUSHs(sv_2mortal(newSVnv(elapse)));

void
events(class)
	SV *class;
	PPCODE:
	pe_event *ev = AllEvents.next->self;
	while (ev) {
	  XPUSHs(sv_2mortal(event_2sv(ev)));
	  ev = ev->all.next->self;
	}

void
restart(class)
	SV *class
	CODE:
	pe_stat_restart();

void
DESTROY()
	CODE:
	pe_stat_stop();

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
# ifdef HAS_SELECT
	  select(0,0,0,0,0);
# else
#  error
# endif
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

void
pe_event::queueEvent(...)
	PREINIT:
	int cnt = 1;
	CODE:
	if (items == 2) cnt = SvIV(ST(1));
	queueEvent(THIS, cnt);

int
doOneEvent()

void
doEvents()
	CODE:
	SV *exitL = perl_get_sv("Event::Loop::ExitLevel", 1);
	SV *loopL = perl_get_sv("Event::Loop::LoopLevel", 1);
	while (SvIVX(exitL) >= SvIVX(loopL))
	  doOneEvent();

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

