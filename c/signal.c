#if !defined(NSIG) || defined(M_UNIX) || defined(M_XENIX)
#include <signal.h>
#endif

static struct pe_event_vtbl pe_signal_vtbl;

typedef struct pe_signal pe_signal;
struct pe_signal {
  pe_event base;
  pe_ring sring;
  int sig;
};

struct pe_sig_stat {
  int valid;  /* waste 4 bytes to know if is a valid entry! */
  pe_ring ring;
  int hits;
};
static struct pe_sig_stat Siginfo[NSIG];
static int Sighits;

static Signal_t
process_sighandler(sig)
int sig;
{
  ++Sighits;  /* is optimization only if signals are rare... */
  ++Siginfo[sig].hits;
}

static pe_event *
pe_signal_allocate()
{
  pe_signal *ev;
  New(PE_NEWID, ev, 1, pe_signal);
  ev->base.vtbl = &pe_signal_vtbl;
  PE_RING_INIT(&ev->sring, ev);
  ev->sig = 0;
  pe_event_init((pe_event*) ev);
  EvREPEAT_on(ev);
  return (pe_event*) ev;
}

static void
pe_signal_start(pe_event *_ev, int repeat)
{
  pe_signal *ev = (pe_signal*) _ev;
  int sig = ev->sig;
  EvSUSPEND_off(ev);
  if (EvACTIVE(ev))
    return;
  if (sig == 0)
    croak("No signal");
  if (PE_RING_EMPTY(&Siginfo[sig].ring))
    rsignal(sig, process_sighandler);
  PE_RING_UNSHIFT(&ev->sring, &Siginfo[sig].ring);
  EvACTIVE_on(ev);
}

static void
pe_signal_stop(pe_event *_ev)
{
  pe_signal *ev = (pe_signal*) _ev;
  int sig = ev->sig;
  if (!EvACTIVE(ev))
    return;
  PE_RING_DETACH(&ev->sring);
  if (PE_RING_EMPTY(&Siginfo[sig].ring))
    rsignal(sig, SIG_DFL);
  EvACTIVE_off(ev);
}

static void
pe_signal_FETCH(pe_event *_ev, SV *svkey)
{
  pe_signal *ev = (pe_signal*) _ev;
  SV *ret=0;
  STRLEN len;
  char *key = SvPV(svkey, len);
  if (len && key[0] == '-') { ++key; --len; }
  if (!len) return;
  switch (key[0]) {
  case 's':
    if (len == 6 && memEQ(key, "signal", 6)) {
      ret = ev->sig > 0? sv_2mortal(newSVpv(Perl_sig_name[ev->sig],0))
	: &sv_undef;
      break;
    }
    break;
  }
  if (ret) {
    dSP;
    XPUSHs(ret);
    PUTBACK;
  } else {
    (*ev->base.vtbl->up->FETCH)(_ev, svkey);
  }
}

static void
pe_signal_STORE(pe_event *_ev, SV *svkey, SV *nval)
{
  pe_signal *ev = (pe_signal*) _ev;
  STRLEN len;
  char *key = SvPV(svkey, len);
  int ok=0;
  if (len && key[0] == '-') { ++key; --len; }
  if (!len) return;
  switch (key[0]) {
  case 's':
    if (len == 6 && memEQ(key, "signal", 6)) {
      int active = EvACTIVE(ev);
      int sig = Perl_whichsig(SvPV(nval,na));
      ok=1;
      if (sig == 0)
	croak("Unrecognized signal");
      if (!Siginfo[sig].valid)
	croak("Signal %d cannot be caught", sig);
      if (active)
	pe_signal_stop(_ev);
      ev->sig = sig;
      if (active)
	pe_signal_start(_ev, 0);
      break;
    }    
    break;
  }
  if (!ok) (_ev->vtbl->up->STORE)(_ev, svkey, nval);
}

/* need atomic test-and-set XXX */

static void
pe_signal_asynccheck()
{
  int xx;
  if (!Sighits)
    return;
  /* mostly harmless race condition for Sighits */
  Sighits = 0;
  for (xx = 1 ; xx < NSIG ; xx++) {
    int got;
    if (!Siginfo[xx].hits)
      continue;
    got = Siginfo[xx].hits;
    /* race condition; might loose signal */
    Siginfo[xx].hits = 0;
    if (got) {
      pe_event *ev = Siginfo[xx].ring.next->self;
      while (ev) {
	queueEvent(ev, got);
	ev = ((pe_signal*)ev)->sring.next->self;
      }
    }
  }
}


static void
boot_signal()
{
  int xx;
  int sig;
  char **sigp;
  static char *keylist[] = {
    "signal"
  };
  static char *nohandle[] = {
    "KILL", "STOP", "ZERO", "CHLD", "CLD", 0
  };
  pe_event_vtbl *vt = &pe_signal_vtbl;
  Zero(Siginfo, NSIG, struct pe_sig_stat);
  Sighits = 0;
  for (xx=0; xx < NSIG; xx++) {
    PE_RING_INIT(&Siginfo[xx].ring, 0);
    Siginfo[xx].valid = 1;
  }
  Siginfo[0].valid = 0;
  sigp = nohandle;
  while (*sigp) {
    sig = Perl_whichsig(*sigp);
    if (sig) Siginfo[sig].valid = 0;
    ++sigp;
  }
  memcpy(vt, &pe_event_base_vtbl, sizeof(pe_event_base_vtbl));
  vt->up = &pe_event_base_vtbl;
  vt->stash = (HV*) SvREFCNT_inc((SV*) gv_stashpv("Event::signal",1));
  vt->keys = sizeof(keylist)/sizeof(char*);
  vt->keylist = keylist;
  vt->FETCH = pe_signal_FETCH;
  vt->STORE = pe_signal_STORE;
  vt->start = pe_signal_start;
  vt->stop = pe_signal_stop;
  vt->invoke = pe_event_invoke_repeat;
  pe_register_vtbl(vt);
  sv_setiv(vt->default_priority, PE_PRIO_HIGH);
}

