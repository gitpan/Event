/* -*- C -*- sometimes */

#define MIN_PERL_DEFINE 1

#ifdef __cplusplus
extern "C" {
#endif

#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>

#ifdef __cplusplus
}
#endif

#include "ppport.h"

/* This is unfortunately necessary for the 5.005_0x series. */
#if PERL_REVISION == 5 && PERL_VERSION <= 5 && PERL_SUBVERSION < 53
#  define PL_vtbl_uvar vtbl_uvar
#  define PL_sig_name sig_name
#endif

#ifdef croak
#  undef croak
#endif
#define croak Event_croak

static void Event_croak(const char* pat, ...) {
    STRLEN n_a;
    dSP;
    SV *msg;
    va_list args;
    /* perl_require_pv("Carp.pm");     Couldn't possibly be unloaded.*/
    va_start(args, pat);
    msg = NEWSV(0,0);
    sv_vsetpvfn(msg, pat, strlen(pat), &args, Null(SV**), 0, 0);
    va_end(args);
    SvREADONLY_on(msg);
    SAVEFREESV(msg);
    PUSHMARK(SP);
    XPUSHs(msg);
    PUTBACK;
    perl_call_pv("Carp::croak", G_DISCARD);
    PerlIO_puts(PerlIO_stderr(), "panic: Carp::croak failed\n");
    (void)PerlIO_flush(PerlIO_stderr());
    my_failure_exit();
}

#ifdef WIN32
#   include <fcntl.h>
#endif

#if defined(HAS_POLL)
# include <poll.h>

/*
	Many operating systems claim to support poll yet they
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

/* Is time() portable everywhere?  Hope so!  XXX */

static double fallback_NVtime()
{ return time(0); }
static void fallback_U2time(U32 *ret)
{ ret[0]=time(0); ret[1]=0; }

#include "Event.h"

static int ActiveWatchers=0; /* includes EvACTIVE + queued events */
static int WarnCounter=16; /*XXX nuke */
static SV *DebugLevel;
static SV *Eval;
static pe_event_stats_vtbl Estat;

/* IntervalEpsilon should be equal to the clock's sleep resolution
   (poll or select) times two.  It probably needs to be bigger if you turn
   on lots of debugging?  Can determine this dynamically? XXX */
static double IntervalEpsilon = 0.0002;
static int TimeoutTooEarly=0;

static double (*myNVtime)();
#define NVtime() (*myNVtime)()

#define EvNOW(exact) NVtime() /*XXX*/

static int pe_sys_fileno(SV *sv, char *context);

static void queueEvent(pe_event *ev);
static void dequeEvent(pe_event *ev);

static void pe_watcher_cancel(pe_watcher *ev);
static void pe_watcher_suspend(pe_watcher *ev);
static void pe_watcher_resume(pe_watcher *ev);
static void pe_watcher_now(pe_watcher *ev);
static void pe_watcher_start(pe_watcher *ev, int repeat);
static void pe_watcher_stop(pe_watcher *ev, int cancel_events);
static void pe_watcher_on(pe_watcher *wa, int repeat);
static void pe_watcher_off(pe_watcher *wa);

/* The newHVhv in perl seems to mysteriously break in some cases.  Here
   is a simple and safe (but maybe slow) implementation. */

#ifdef newHVhv
# undef newHVhv
#endif
#define newHVhv event_newHVhv

static HV *event_newHVhv(HV *ohv) {
    register HV *hv = newHV();
    register HE *entry;
    hv_iterinit(ohv);		/* NOTE: this resets the iterator */
    while (entry = hv_iternext(ohv)) {
	hv_store(hv, HeKEY(entry), HeKLEN(entry), 
		SvREFCNT_inc(HeVAL(entry)), HeHASH(entry));
    }
    return hv;
}

static void pe_watcher_STORE_FALLBACK(pe_watcher *wa, SV *svkey, SV *nval)
{
    if (!wa->FALLBACK)
	wa->FALLBACK = newHV();
    hv_store_ent(wa->FALLBACK, svkey, SvREFCNT_inc(nval), 0);
}

/***************** STATS */
static int StatsInstalled=0;
static void pe_install_stats(pe_event_stats_vtbl *esvtbl) {
    ++StatsInstalled;
    Copy(esvtbl, &Estat, 1, pe_event_stats_vtbl);
    Estat.on=0;
}
static void pe_collect_stats(int yes) {
    if (!StatsInstalled)
	croak("collect_stats: no event statistics are available");
    Estat.on = yes;
}

#ifdef HAS_GETTIMEOFDAY
double null_loops_per_second(int sec)
{
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
return count/sec;
}
#else /* !HAS_GETTIMEOFDAY */
double null_loops_per_second(int sec)
{ croak("sorry, gettimeofday is not available"); }
#endif


#include "typemap.c"
#include "timeable.c"
#include "hook.c"
#include "ev.c"
#include "watcher.c"
#include "idle.c"
#include "timer.c"
#include "io.c"
#include "unix_io.c"
#include "var.c"
#include "signal.c"
#include "tied.c"
#include "queue.c"

MODULE = Event		PACKAGE = Event

PROTOTYPES: DISABLE

BOOT:
  DebugLevel = SvREFCNT_inc(perl_get_sv("Event::DebugLevel", 1));
  Eval = SvREFCNT_inc(perl_get_sv("Event::Eval", 1));
  Estat.on=0;
  boot_timeable();
  boot_hook();
  boot_pe_event();
  boot_pe_watcher();
  boot_idle();
  boot_timer();
  boot_io();
  boot_var();
  boot_tied();
  boot_signal();
  boot_queue();
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
      api->stop = pe_watcher_stop;
      api->cancel = pe_watcher_cancel;
      api->tstart = pe_timeable_start;
      api->tstop  = pe_timeable_stop;
      api->new_idle =   (pe_idle*  (*)(HV*,SV*))    pe_idle_allocate;
      api->new_timer =  (pe_timer* (*)(HV*,SV*))    pe_timer_allocate;
      api->new_io =     (pe_io*    (*)(HV*,SV*))    pe_io_allocate;
      api->new_var =    (pe_var*   (*)(HV*,SV*))    pe_var_allocate;
      api->new_signal = (pe_signal*(*)(HV*,SV*))    pe_signal_allocate;
      api->add_hook = capi_add_hook;
      api->cancel_hook = pe_cancel_hook;
      api->install_stats = pe_install_stats;
      api->collect_stats = pe_collect_stats;
      api->AllWatchers = &AllWatchers;
      api->watcher_2sv = watcher_2sv;
      api->sv_2watcher = sv_2watcher;
      api->event_2sv = event_2sv;
      api->sv_2event = sv_2event;
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

int
_timeout_too_early()
	CODE:
	RETVAL = TimeoutTooEarly;
	TimeoutTooEarly=0;
	OUTPUT:
	RETVAL

bool
cache_time_api()
	CODE:
	SV **svp = hv_fetch(PL_modglobal, "Time::NVtime", 12, 0);
	if (!svp || !*svp || !SvIOK(*svp))
	    XSRETURN_NO;
	myNVtime = (double(*)()) SvIV(*svp);
	XSRETURN_YES;

void
install_time_api()
	CODE:
	SV **svp = hv_fetch(PL_modglobal, "Time::NVtime", 12, 0);
	svp = hv_store(PL_modglobal, "Time::NVtime", 12,
			 newSViv((IV) fallback_NVtime), 0);
	hv_store(PL_modglobal, "Time::U2time", 12,
			 newSViv((IV) fallback_U2time), 0);

double
time()
	PROTOTYPE:
	CODE:
	RETVAL = NVtime();
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

void
all_watchers()
	PROTOTYPE:
	PPCODE:
	pe_watcher *ev;
	if (!AllWatchers.next)
	  return;
	ev = (pe_watcher*) AllWatchers.next->self;
	while (ev) {
	  XPUSHs(watcher_2sv(ev));
	  ev = (pe_watcher*) ev->all.next->self;
	}

void
all_idle()
	PROTOTYPE:
	PPCODE:
	pe_watcher *ev;
	if (!Idle.prev)
	  return;
	ev = (pe_watcher*) Idle.prev->self;
	while (ev) {
	  XPUSHs(watcher_2sv(ev));
	  ev = (pe_watcher*) ((pe_idle*)ev)->iring.prev->self;
	}

void
all_running()
	PROTOTYPE:
	PPCODE:
	int fx;
	for (fx = CurCBFrame; fx >= 0; fx--) {
	  pe_watcher *ev = (CBFrame + fx)->ev->up; /* XXX */
	  XPUSHs(watcher_2sv(ev));
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
	wa = (pe_watcher*) sv_2watcher(ST(0));
	if (items == 1) {
	    ev = (*wa->vtbl->new_event)(wa);
	    ++ev->hits;
	}
	else if (items == 2) {
	  if (SvNIOK(ST(1))) {
	    ev = (*wa->vtbl->new_event)(wa);
	    ev->hits += SvIV(ST(1));
	  }
	  else {
	    ev = (pe_event*) sv_2event(ST(1));
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


MODULE = Event		PACKAGE = Event::Event::Io

void
pe_event::got()
	PPCODE:
	XPUSHs(sv_2mortal(events_mask_2sv(((pe_ioevent*)THIS)->got)));

MODULE = Event		PACKAGE = Event::Event

void
DESTROY(ref)
	SV *ref;
	CODE:
{
	pe_event *THIS = (pe_event*) sv_2event(ref);
	/*
	if (EvDEBUGx(THIS) >= 4) {
	    STRLEN n_a;
	    warn("Event=0x%x '%s' DESTROY SV=0x%x",
		 THIS, SvPV(THIS->up->desc, n_a), SvRV(THIS->mysv));
	}
	*/
	(*THIS->vtbl->dtor)(THIS);
}

void
pe_event::mom()
	PPCODE:
	if (--WarnCounter >= 0) warn("'mom' renamed to 'w'");
	XPUSHs(watcher_2sv(THIS->up));

void
pe_event::w()
	PPCODE:
	XPUSHs(watcher_2sv(THIS->up));

void
pe_event::hits()
	PPCODE:
	XPUSHs(sv_2mortal(newSViv(THIS->hits)));

void
pe_event::prio()
	PPCODE:
	XPUSHs(sv_2mortal(newSViv(THIS->prio)));

MODULE = Event		PACKAGE = Event::Watcher

void
DESTROY(ref)
	SV *ref;
	CODE:
{
	pe_watcher *THIS = (pe_watcher*) sv_2watcher(ref);
	assert(THIS);
	if (THIS->mysv) {
	    THIS->mysv=0;
	    if (EvCANDESTROY(THIS)) /*mysv*/
		(*THIS->vtbl->dtor)(THIS);
	}
}

void
pe_watcher::again()
	CODE:
	pe_watcher_start(THIS, 1);

void
pe_watcher::start()
	CODE:
	pe_watcher_start(THIS, 0);

void
pe_watcher::suspend(...)
	CODE:
	if (items == 2) {
	    if (sv_true(ST(1)))
		pe_watcher_suspend(THIS);
	    else
		pe_watcher_resume(THIS);
	} else {
	    warn("Ambiguous use of suspend"); /*XXX*/
	    pe_watcher_suspend(THIS);
	    XSRETURN_YES;
	}

void
pe_watcher::resume()
	CODE:
	warn("Please use $w->suspend(0) instead of resume"); /* DEPRECATED */
	pe_watcher_resume(THIS);

void
pe_watcher::stop()
	CODE:
	pe_watcher_stop(THIS, 1);

void
pe_watcher::cancel()
	CODE:
	pe_watcher_cancel(THIS);

void
pe_watcher::now()
	CODE:
	pe_watcher_now(THIS);

void
pe_watcher::use_keys(...)
	PREINIT:
	PPCODE:
	warn("use_keys is deprecated");

void
pe_watcher::running(...)
	PPCODE:
	PUTBACK;
	warn("running renamed to is_running");
	_watcher_running(THIS, items == 2? sv_mortalcopy(ST(1)) : 0);
	SPAGAIN;

void
pe_watcher::is_running(...)
	PPCODE:
	PUTBACK;
	_watcher_running(THIS, items == 2? sv_mortalcopy(ST(1)) : 0);
	SPAGAIN;

void
pe_watcher::is_active(...)
	PPCODE:
	PUTBACK;
	XPUSHs(boolSV(EvACTIVE(THIS)));

void
pe_watcher::is_suspended(...)
	PPCODE:
	PUTBACK;
	XPUSHs(boolSV(EvSUSPEND(THIS)));

void
pe_watcher::is_queued(...)
	PPCODE:
	PUTBACK;
	XPUSHs(boolSV(EvFLAGS(THIS) & PE_QUEUED));

void
pe_watcher::cb(...)
	PPCODE:
	PUTBACK;
	_watcher_callback(THIS, items == 2? sv_mortalcopy(ST(1)) : 0);
	SPAGAIN;

void
pe_watcher::cbtime(...)
	PPCODE:
	PUTBACK;
	_watcher_cbtime(THIS, items == 2? sv_mortalcopy(ST(1)) : 0);
	SPAGAIN;

void
pe_watcher::clump(...)
	PPCODE:
	PUTBACK;
	_watcher_clump(THIS, items == 2? sv_mortalcopy(ST(1)) : 0);
	SPAGAIN;

void
pe_watcher::desc(...)
	PPCODE:
	PUTBACK;
	_watcher_desc(THIS, items == 2? sv_mortalcopy(ST(1)) : 0);
	SPAGAIN;

void
pe_watcher::debug(...)
	PPCODE:
	PUTBACK;
	_watcher_debug(THIS, items == 2? sv_mortalcopy(ST(1)) : 0);
	SPAGAIN;

void
pe_watcher::prio(...)
	PPCODE:
	PUTBACK;
	_watcher_priority(THIS, items == 2? sv_mortalcopy(ST(1)) : 0);
	SPAGAIN;

void
pe_watcher::reentrant(...)
	PPCODE:
	PUTBACK;
	_watcher_reentrant(THIS, items == 2? sv_mortalcopy(ST(1)) : 0);
	SPAGAIN;

void
pe_watcher::repeat(...)
	PPCODE:
	PUTBACK;
	_watcher_repeat(THIS, items == 2? sv_mortalcopy(ST(1)) : 0);
	SPAGAIN;

void
pe_watcher::max_cb_tm(...)
	PPCODE:
	PUTBACK;
	_watcher_max_cb_tm(THIS, items == 2? sv_mortalcopy(ST(1)) : 0);
	SPAGAIN;

MODULE = Event		PACKAGE = Event::Watcher::Tied

void
allocate(clname, temple)
	SV *clname
	SV *temple
	PPCODE:
	if (!SvROK(temple)) croak("Bad template");
	XPUSHs(watcher_2sv(pe_tied_allocate(gv_stashsv(clname, 1),
					    SvRV(temple))));

void
pe_watcher::hard(...)
	PPCODE:
	PUTBACK;
	_timeable_hard(THIS, items == 2? sv_mortalcopy(ST(1)) : 0);
	SPAGAIN;

void
pe_watcher::at(...)
	PPCODE:
	PUTBACK;
	_tied_at(THIS, items == 2? sv_mortalcopy(ST(1)) : 0);
	SPAGAIN;

void
pe_watcher::flags(...)
	PPCODE:
	PUTBACK;
	_tied_flags(THIS, items == 2? sv_mortalcopy(ST(1)) : 0);
	SPAGAIN;

MODULE = Event		PACKAGE = Event::idle

void
allocate(clname, temple)
	SV *clname;
	SV *temple;
	PPCODE:
	if (!SvROK(temple)) croak("Bad template");
	XPUSHs(watcher_2sv(pe_idle_allocate(gv_stashsv(clname, 1),
			SvRV(temple))));

void
pe_watcher::hard(...)
	PPCODE:
	PUTBACK;
	_timeable_hard(THIS, items == 2? sv_mortalcopy(ST(1)) : 0);
	SPAGAIN;

void
pe_watcher::max(...)
	PPCODE:
	PUTBACK;
	_idle_max_interval(THIS, items == 2? sv_mortalcopy(ST(1)) : 0);
	SPAGAIN;

void
pe_watcher::min(...)
	PPCODE:
	PUTBACK;
	_idle_min_interval(THIS, items == 2? sv_mortalcopy(ST(1)) : 0);
	SPAGAIN;

MODULE = Event		PACKAGE = Event::timer

void
allocate(clname, temple)
	SV *clname;
	SV *temple;
	PPCODE:
	XPUSHs(watcher_2sv(pe_timer_allocate(gv_stashsv(clname, 1),
			SvRV(temple))));

void
pe_watcher::at(...)
	PPCODE:
	PUTBACK;
	_timer_at(THIS, items == 2? sv_mortalcopy(ST(1)) : 0);
	SPAGAIN;

void
pe_watcher::hard(...)
	PPCODE:
	PUTBACK;
	_timeable_hard(THIS, items == 2? sv_mortalcopy(ST(1)) : 0);
	SPAGAIN;

void
pe_watcher::interval(...)
	PPCODE:
	PUTBACK;
	_timer_interval(THIS, items == 2? sv_mortalcopy(ST(1)) : 0);
	SPAGAIN;

MODULE = Event		PACKAGE = Event::io

void
allocate(clname, temple)
	SV *clname;
	SV *temple;
	PPCODE:
	if (!SvROK(temple)) croak("Bad template");
	XPUSHs(watcher_2sv(pe_io_allocate(gv_stashsv(clname, 1),
			SvRV(temple))));

void
pe_watcher::poll(...)
	PPCODE:
	PUTBACK;
	_io_poll(THIS, items == 2? sv_mortalcopy(ST(1)) : 0);
	SPAGAIN;

void
pe_watcher::fd(...)
	PPCODE:
	PUTBACK;
	_io_handle(THIS, items == 2? sv_mortalcopy(ST(1)) : 0);
	SPAGAIN;

void
pe_watcher::timeout(...)
	PPCODE:
	PUTBACK;
	_io_timeout(THIS, items == 2? sv_mortalcopy(ST(1)) : 0);
	SPAGAIN;

MODULE = Event		PACKAGE = Event::var

void
allocate(clname, temple)
	SV *clname;
	SV *temple;
	PPCODE:
	XPUSHs(watcher_2sv(pe_var_allocate(gv_stashsv(clname, 1),
		SvRV(temple))));

void
pe_watcher::var(...)
	PPCODE:
	PUTBACK;
	_var_variable(THIS, items == 2? ST(1) : 0); /* don't mortalcopy!! */
	SPAGAIN;

void
pe_watcher::poll(...)
	PPCODE:
	PUTBACK;
	_var_events(THIS, items == 2? sv_mortalcopy(ST(1)) : 0);
	SPAGAIN;

MODULE = Event		PACKAGE = Event::signal

void
allocate(clname, temple)
	SV *clname;
	SV *temple;
	PPCODE:
	XPUSHs(watcher_2sv(pe_signal_allocate(gv_stashsv(clname, 1),
		SvRV(temple))));

void
pe_watcher::signal(...)
	PPCODE:
	PUTBACK;
	_signal_signal(THIS, items == 2? sv_mortalcopy(ST(1)) : 0);
	SPAGAIN;
