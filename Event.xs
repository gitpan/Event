/* -*- /C/ -*- sometimes */

/*
void
all_queued()
	PROTOTYPE:
	PPCODE:
	pe_watcher *ev;
	ev = NQueue.next->self;
	while (ev) {
	  XPUSHs(sv_2mortal(watcher_2sv(ev)));
	  ev = ev->que.next->self;
	}


*/


#define MIN_PERL_DEFINE 1

#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>

#include "patchlevel.h"
#if SUBVERSION < 53
#  define PL_vtbl_uvar vtbl_uvar
#  define PL_sig_name sig_name
#endif

#ifdef WIN32
#   include <fcntl.h>
#endif

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

static int ActiveWatchers=0; /* includes EvACTIVE + queued events */
static int WarnCounter=10;
static int Stats=0;
static SV *DebugLevel;
static SV *Eval;

/* close enough to zero -- this needs to be bigger if you turn
   on lots of debugging?  Can determine clock resolution dynamically? XXX */
static double IntervalEpsilon = 0.0001;
static int TimeoutTooEarly=0;

static void queueEvent(pe_event *ev);
static void dequeEvent(pe_event *ev);

static int pe_sys_fileno(pe_io *ev);
static void pe_watcher_cancel(pe_watcher *ev);
static void pe_watcher_suspend(pe_watcher *ev);
static void pe_watcher_resume(pe_watcher *ev);
static void pe_watcher_now(pe_watcher *ev);
static void pe_watcher_start(pe_watcher *ev, int repeat);
static void pe_watcher_stop(pe_watcher *ev);
static void pe_watcher_on(pe_watcher *wa, int repeat);
static void pe_watcher_off(pe_watcher *wa);

#include "typemap.c"
#include "gettimeofday.c"  /* hack? XXX */
#include "timeable.c"
#include "event.c"
#include "watcher.c"
#include "idle.c"
#include "timer.c"
#include "io.c"
#include "unix_io.c"
#include "var.c"
#include "signal.c"
#include "tied.c"
#include "queue.c"
#include "stats.c"

MODULE = Event		PACKAGE = Event

PROTOTYPES: DISABLE

BOOT:
  DebugLevel = SvREFCNT_inc(perl_get_sv("Event::DebugLevel", 1));
  Eval = SvREFCNT_inc(perl_get_sv("Event::Eval", 1));
  boot_typemap();
  boot_gettimeofday();
  boot_timeable();
  boot_pe_event();
  boot_pe_watcher();
  boot_idle();
  boot_timer();
  boot_io();
  boot_var();
  boot_tied();
  boot_signal();
  boot_queue();
  boot_stats();
  {
    struct EventAPI *api;
    SV *apisv;
    New(PE_NEWID, api, 1, struct EventAPI);
    api->Ver = EventAPI_VERSION;
    api->start = pe_watcher_start;
    api->queue = queueEvent;
    api->now = pe_watcher_now;
    api->suspend = pe_watcher_suspend;
    api->resume = pe_watcher_resume;
    api->cancel = pe_watcher_cancel;
    api->tstart = pe_timeable_start;
    api->tstop  = pe_timeable_stop;
    api->new_idle =     (pe_idle*(*)())         pe_idle_allocate;
    api->new_timer =    (pe_timer*(*)())       pe_timer_allocate;
    api->new_io =       (pe_io*(*)())             pe_io_allocate;
    api->new_var =      (pe_var*(*)())           pe_var_allocate;
    api->new_signal =   (pe_signal*(*)())     pe_signal_allocate;
    api->add_hook = capi_add_hook;
    api->cancel_hook = pe_cancel_hook;
    apisv = perl_get_sv("Event::API", 1);
    sv_setiv(apisv, (IV)api);
    SvREADONLY_on(apisv);
  }

void
_add_hook(type, code)
	char *type
	SV *code
	CODE:
	pe_add_hook(type, 1, code, 0);
	/* would be nice to return new pe_qcallback* XXX */

void
time()
	PROTOTYPE:
	PPCODE:
	pe_cache_now();
	XPUSHs(NowSV);

int
_timeout_too_early()
	CODE:
	RETVAL = TimeoutTooEarly;
	TimeoutTooEarly=0;
	OUTPUT:
	RETVAL

void
sleep(tm)
	double tm;
	PROTOTYPE: $
	CODE:
	pe_sys_sleep(tm);

double
null_loops_per_second(sec)
	int sec
	CODE:
	/*
	  This should be more realistic.  It is used to normalize
	  the benchmark against some theoretical perfect event loop.
	*/
	struct timeval start_tm, done_tm;
	double elapse;
	unsigned count=0;
	int fds[2];
	if (pipe(fds) != 0) croak("pipe");
	gettimeofday(&start_tm, 0);
	do {
#ifdef HAS_POLL
	  struct pollfd map[2];
	  Zero(map, 2, struct pollfd);
	  map[0].fd = fds[0];
	  map[0].events = POLLIN | POLLOUT;
	  map[0].revents = 0;
	  map[1].fd = fds[1];
	  map[1].events = POLLIN | POLLOUT;
	  map[1].revents = 0;
	  poll(map, 2, 0);
#elif defined(HAS_SELECT)
	  struct timeval null;
	  fd_set rfds, wfds, efds;
	  FD_ZERO(&rfds);
	  FD_ZERO(&wfds);
	  FD_ZERO(&efds);
	  FD_SET(fds[0], &rfds);
	  FD_SET(fds[0], &wfds);
	  FD_SET(fds[1], &rfds);
	  FD_SET(fds[1], &wfds);
	  null.tv_sec = 0;
	  null.tv_usec = 0;
	  select(3,&rfds,&wfds,&efds,&null);
#else
#  error
#endif
	  ++count;
	  gettimeofday(&done_tm, 0);
	  elapse = (done_tm.tv_sec - start_tm.tv_sec +
		    (done_tm.tv_usec - start_tm.tv_usec) / 1000000);
	} while(elapse < sec);
	close(fds[0]);
	close(fds[1]);
	RETVAL = count/sec;
	OUTPUT:
	RETVAL

void
all_watchers()
	PROTOTYPE:
	PPCODE:
	pe_watcher *ev;
	if (!AllWatchers.next)
	  return;
	ev = AllWatchers.next->self;
	while (ev) {
	  XPUSHs(sv_2mortal(watcher_2sv(ev)));
	  ev = ev->all.next->self;
	}

void
all_idle()
	PROTOTYPE:
	PPCODE:
	pe_watcher *ev;
	if (!Idle.prev)
	  return;
	ev = Idle.prev->self;
	while (ev) {
	  XPUSHs(sv_2mortal(watcher_2sv(ev)));
	  ev = ((pe_idle*)ev)->iring.prev->self;
	}

void
all_running()
	PROTOTYPE:
	PPCODE:
	int fx;
	for (fx = CurCBFrame; fx >= 0; fx--) {
	  pe_watcher *ev = (CBFrame + fx)->ev->up; /* XXX */
	  XPUSHs(sv_2mortal(watcher_2sv(ev)));
	  if (GIMME_V != G_ARRAY)
	    break;
	}

void
queue(...)
	PROTOTYPE: $;$
	PREINIT:
	pe_watcher *wa;
	pe_event *ev;
	int cnt = 1;
	PPCODE:
	decode_sv(ST(0), &wa, 0);
	if (items == 1) {
	    ev = (*wa->vtbl->new_event)(wa);
	    ++ev->count;
	}
	else if (items == 2) {
	  if (SvNIOK(ST(1))) {
	    ev = (*wa->vtbl->new_event)(wa);
	    ev->count += SvIV(ST(1));
	  }
	  else {
	    decode_sv(ST(1), 0, &ev);
	    if (ev->up != wa)
	      croak("queue: event doesn't match watcher");
	  }
	}
	queueEvent(ev);

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
_loop(...)
	PROTOTYPE: ;$
	CODE:
	SV *exitL = perl_get_sv("Event::ExitLevel", 1);
	SV *loopL = perl_get_sv("Event::LoopLevel", 1);
	pe_check_recovery();
	assert(SvIOK(exitL) && SvIOK(loopL));
	while (SvIVX(exitL) >= SvIVX(loopL) && ActiveWatchers) {
	  ENTER;
	  SAVETMPS;
	  one_event(60);
	  FREETMPS;
	  LEAVE;
	}

void
_queue_pending()
	CODE:
	pe_queue_pending();

int
_empty_queue(prio)
	int prio
	CODE:
	pe_check_recovery();
	while (pe_empty_queue(prio));

void
_check_recovery()
	CODE:
	pe_check_recovery();

void
queue_time(prio)
	int prio
	PPCODE:
	double max=0;
	int xx;
	if (prio < 0 || prio >= PE_QUEUES)
	  croak("queue_time(%d) out of domain [0..%d]",
		prio, PE_QUEUES-1);
	for (xx=0; xx <= prio; xx++)
	  if (max < QueueTime[xx]) max = QueueTime[xx];
	XPUSHs(max? sv_2mortal(newSVnv(max)) : &PL_sv_undef);


MODULE = Event		PACKAGE = Event::Watcher::Inner

void
DESTROY(ref)
	SV *ref
	CODE:
	SV *sv;
	pe_watcher *THIS;
	assert(SvRV(ref));
	sv = SvRV(ref);
	assert(SvTYPE(sv) == SVt_PVMG);
	THIS = (pe_watcher*) SvIV(sv);
	--THIS->refcnt;
	/*  warn("id=%d --refcnt=%d flags=0x%x",
		THIS->id, THIS->refcnt,THIS->flags); /**/
	if (EvCANDESTROY(THIS)) {
	  (*THIS->vtbl->dtor)(THIS);
	}

MODULE = Event		PACKAGE = Event::Watcher

void
pe_watcher::again()
	CODE:
	pe_watcher_start(THIS, 1);

void
pe_watcher::start()
	CODE:
	pe_watcher_start(THIS, 0);

void
pe_watcher::suspend()
	CODE:
	pe_watcher_suspend(THIS);

void
pe_watcher::resume()
	CODE:
	pe_watcher_resume(THIS);

void
pe_watcher::cancel()
	CODE:
	pe_watcher_cancel(THIS);

void
pe_watcher::now()
	CODE:
	pe_watcher_now(THIS);

void
FETCH(obj, key)
	SV *obj;
	SV *key;
	PREINIT:
	void *vp;
	pe_base_vtbl *vt;
	PPCODE:
	PUTBACK;
	get_base_vtbl(obj, &vp, &vt);
	vt->Fetch(vp, key);
	SPAGAIN;

void
STORE(obj, key, nval)
	SV *obj
	SV *key
	SV *nval
	PREINIT:
	void *vp;
	pe_base_vtbl *vt;
	PPCODE:
	PUTBACK;
	get_base_vtbl(obj, &vp, &vt);
	vt->Store(vp, key, nval);
	SPAGAIN;

void
FIRSTKEY(obj)
	SV *obj
	PREINIT:
	void *vp;
	pe_base_vtbl *vt;
	PPCODE:
	PUTBACK;
	get_base_vtbl(obj, &vp, &vt);
	vt->Firstkey(vp);
	SPAGAIN;

void
NEXTKEY(obj, prevkey)
	SV *obj;
	SV *prevkey
	PREINIT:
	void *vp;
	pe_base_vtbl *vt;
	PPCODE:
	PUTBACK;
	get_base_vtbl(obj, &vp, &vt);
	vt->Nextkey(vp);
	SPAGAIN;

void
pe_watcher::DELETE(key)
	SV *key
	PPCODE:
	PUTBACK;
	pe_watcher_DELETE(THIS, key);
	SPAGAIN;

void
pe_watcher::EXISTS(key)
	SV *key
	PPCODE:
	PUTBACK;
	pe_watcher_EXISTS(THIS, key);
	SPAGAIN;

void
pe_watcher::stats(sec)
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

pe_watcher *
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

pe_watcher *
allocate()
	CODE:
	RETVAL = pe_idle_allocate();
	OUTPUT:
	RETVAL


MODULE = Event		PACKAGE = Event::timer

pe_watcher *
allocate()
	CODE:
	RETVAL = pe_timer_allocate();
	OUTPUT:
	RETVAL


MODULE = Event		PACKAGE = Event::io

pe_watcher *
allocate()
	CODE:
	RETVAL = pe_io_allocate();
	OUTPUT:
	RETVAL


MODULE = Event		PACKAGE = Event::var

pe_watcher *
allocate()
	CODE:
	RETVAL = pe_var_allocate();
	OUTPUT:
	RETVAL


MODULE = Event		PACKAGE = Event::signal

pe_watcher *
allocate()
	CODE:
	RETVAL = pe_signal_allocate();
	OUTPUT:
	RETVAL

