/* -*- /C/ -*- sometimes */

#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>

#define PE_NEWID ('e'+'v')  /* for New() macro */

#if defined(HAS_POLL)
# include <poll.h>

/*
	Many operating systems claim to support poll, yet they
	actually emulate it with select.  c/unix_io.c supports
	either poll or select but it doesn't know which one to
	use.  Here we try to detect if we have a native poll
	implementation.  If we do, we use it.  Otherwise,
	select is assumed.
*/

# ifndef POLLOUT
#  undef HAS_POLL
# endif
# ifndef POLLWRNORM
#  undef HAS_POLL
# endif
# ifndef POLLWRBAND
#  undef HAS_POLL
# endif
#endif

#include "Event.h"

static int Stats=0;
static SV *DebugLevel;
static SV *Eval;

static void queueEvent(pe_event *ev, int count);
static void dequeEvent(pe_event *ev);

static void pe_event_cancel(pe_event *ev);
static void pe_event_suspend(pe_event *ev);
static void pe_event_now(pe_event *ev);
static void pe_event_start(pe_event *ev, int repeat);

#include "typemap.c"
#include "gettimeofday.c"  /* hack XXX */
#include "timeable.c"
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
  boot_gettimeofday();
  boot_timeable();
  boot_pe_event();
  boot_idle();
  boot_timer();
  boot_io();
  boot_watchvar();
  boot_tied();
  boot_signal();
  boot_queue();
  boot_stats();
  {
    struct EventAPI *api;
    SV *apisv;
    New(PE_NEWID, api, 1, struct EventAPI);
    api->Ver = EventAPI_VERSION;
    api->one_event = safe_one_event;
    api->start = pe_event_start;
    api->queue = queueEvent;
    api->now = pe_event_now;
    api->suspend = pe_event_suspend;
    api->resume = pe_event_resume;
    api->cancel = pe_event_cancel;
    api->new_idle =     (pe_idle*(*)())         pe_idle_allocate;
    api->new_timer =    (pe_timer*(*)())       pe_timer_allocate;
    api->new_io =       (pe_io*(*)())             pe_io_allocate;
    api->new_watchvar = (pe_watchvar*(*)()) pe_watchvar_allocate;
    api->new_signal =   (pe_signal*(*)())     pe_signal_allocate;
    apisv = perl_get_sv("Event::API", 1);
    sv_setiv(apisv, (IV)api);
    SvREADONLY_on(apisv);
  }

int
_sizeof()
	CODE:
	RETVAL = sizeof(pe_event);
	OUTPUT:
	RETVAL

void
time()
	PROTOTYPE:
	PPCODE:
	pe_cache_now();
	XPUSHs(NowSV);

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
#elif defined(HAS_SELECT)
	  struct timeval null;
	  null.tv_sec = 0;
	  null.tv_usec = 0;
	  select(0,0,0,0,&null);
#else
#  error
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
all_events()
	PROTOTYPE:
	PPCODE:
	pe_event *ev = AllEvents.next->self;
	while (ev) {
	  XPUSHs(sv_2mortal(event_2sv(ev)));
	  ev = ev->all.next->self;
	}

void
all_running()
	PROTOTYPE:
	PPCODE:
	int fx;
	for (fx = CurCBFrame; fx >= 0; fx--) {
	  pe_event *ev = (CBFrame + fx)->ev;
	  XPUSHs(sv_2mortal(event_2sv(ev)));
	  if (GIMME_V != G_ARRAY)
	    break;
	}

void
all_queued()
	PROTOTYPE:
	PPCODE:
	pe_event *ev;
	ev = NQueue.next->self;
	while (ev) {
	  XPUSHs(sv_2mortal(event_2sv(ev)));
	  ev = ev->que.next->self;
	}

void
all_idle()
	PROTOTYPE:
	PPCODE:
	pe_event *ev;
	ev = Idle.prev->self;
	while (ev) {
	  XPUSHs(sv_2mortal(event_2sv(ev)));
	  ev = ev->que.prev->self;
	}

void
pe_event::queueEvent(...)
	PROTOTYPE: $;$
	PREINIT:
	int cnt = 1;
	CODE:
	if (items == 2) cnt = SvIV(ST(1));
	queueEvent(THIS, cnt);

int
one_event(...)
	PROTOTYPE: ;$
	CODE:
	double maxtm = 60;
	if (items == 1) maxtm = SvNV(ST(0));
	RETVAL = safe_one_event(maxtm);
	OUTPUT:
	RETVAL

void
_loop()
	PROTOTYPE:
	CODE:
	SV *exitL = perl_get_sv("Event::ExitLevel", 1);
	SV *loopL = perl_get_sv("Event::LoopLevel", 1);
	pe_check_recovery();
	assert(SvIOK(exitL) && SvIOK(loopL));
	while (SvIVX(exitL) >= SvIVX(loopL))
	  one_event(60);

void
_check_recovery()
	CODE:
	pe_check_recovery();


MODULE = Event		PACKAGE = Event::Watcher

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
	if (THIS->stats)
	  pe_stat_query(THIS->stats, sec, &ran, &elapse);
	else
	  ran = elapse = 0;
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

int
round_seconds(sec)
	int sec;
	CODE:
	if (sec <= 0)
	  RETVAL = PE_STAT_SECONDS;
	else if (sec < PE_STAT_SECONDS * PE_STAT_I1)
	  RETVAL = ((int)(sec + PE_STAT_SECONDS-1)/ PE_STAT_SECONDS) *
			PE_STAT_SECONDS;
	else if (sec < PE_STAT_SECONDS * PE_STAT_I1 * PE_STAT_I2)
	  RETVAL = ((int)(sec + PE_STAT_SECONDS * PE_STAT_I1 - 1) /
			       (PE_STAT_SECONDS * PE_STAT_I1)) *
			PE_STAT_SECONDS * PE_STAT_I1;
	else
	  RETVAL = PE_STAT_SECONDS * PE_STAT_I1 * PE_STAT_I2;
	OUTPUT:
	RETVAL

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
total(class, sec)
	SV *class;
	int sec
	PREINIT:
	int ran;
	double elapse;
	PPCODE:
	pe_stat_query(&totalStats, sec, &ran, &elapse);
	XPUSHs(sv_2mortal(newSVnv(elapse)));

void
restart(class)
	SV *class
	CODE:
	pe_stat_restart();

void
DESTROY()
	CODE:
	pe_stat_stop();

MODULE = Event		PACKAGE = Event::idle

pe_event *
allocate()
	CODE:
	RETVAL = pe_idle_allocate();
	OUTPUT:
	RETVAL


MODULE = Event		PACKAGE = Event::timer

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

