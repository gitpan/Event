#if !defined(NSIG) || defined(M_UNIX) || defined(M_XENIX)
#include <signal.h>
#endif

static struct pe_event_vtbl pe_signal_vtbl;

/* GLOBALS: Sigvalid Sigring Sigstat Sigslot */

static U32 Sigvalid[1+NSIG/32]; /*assume 32bit; doesn't matter*/
#define PE_SIGVALID(sig)  	(Sigvalid[sig>>5] & (1 << ((sig) & 0x1f)))
#define PE_SIGVALID_off(sig)	Sigvalid[sig>>5] &= ~(1 << ((sig) & 0x1f))

struct pe_sig_stat {
  U32 Hits;
  U16 hits[NSIG];
};
typedef struct pe_sig_stat pe_sig_stat;

static int Sigslot;
static pe_sig_stat Sigstat[2];

static pe_ring Sigring[NSIG];

/* /GLOBALS */

static Signal_t process_sighandler(int sig)
{
  pe_sig_stat *st = &Sigstat[Sigslot];
  ++st->Hits;
  ++st->hits[sig];
}

static pe_event *pe_signal_allocate()
{
  pe_signal *ev;
  New(PE_NEWID, ev, 1, pe_signal);
  ev->base.vtbl = &pe_signal_vtbl;
  PE_RING_INIT(&ev->sring, ev);
  ev->signal = 0;
  pe_event_init((pe_event*) ev);
  EvREPEAT_on(ev);
  EvINVOKE1_off(ev);
  return (pe_event*) ev;
}

static void pe_signal_start(pe_event *_ev, int repeat)
{
  pe_signal *ev = (pe_signal*) _ev;
  int sig = ev->signal;
  if (sig == 0)
    croak("No signal");
  if (PE_RING_EMPTY(&Sigring[sig]))
    rsignal(sig, process_sighandler);
  PE_RING_UNSHIFT(&ev->sring, &Sigring[sig]);
}

static void pe_signal_stop(pe_event *_ev)
{
  pe_signal *ev = (pe_signal*) _ev;
  int sig = ev->signal;
  PE_RING_DETACH(&ev->sring);
  if (PE_RING_EMPTY(&Sigring[sig]))
    rsignal(sig, SIG_DFL);
}

static void pe_signal_FETCH(pe_event *_ev, SV *svkey)
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
      ret = ev->signal > 0? sv_2mortal(newSVpv(Perl_sig_name[ev->signal],0))
	: &PL_sv_undef;
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

static void pe_signal_STORE(pe_event *_ev, SV *svkey, SV *nval)
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
      int sig = Perl_whichsig(SvPV(nval,PL_na));
      /*warn("whichsig(%s) = %d", SvPV(nval,na), sig); /**/
      ok=1;
      if (sig == 0)
	croak("Unrecognized signal '%s'", SvPV(nval,PL_na));
      if (!PE_SIGVALID(sig))
	croak("Signal '%s' cannot be caught", SvPV(nval,PL_na));
      if (active)
	pe_event_stop(_ev);
      ev->signal = sig;
      if (active)
	pe_event_start(_ev, 0);
      break;
    }    
    break;
  }
  if (!ok) (_ev->vtbl->up->STORE)(_ev, svkey, nval);
}


static void _signal_asynccheck(pe_sig_stat *st)
{
  int xx, got;
  pe_event *ev;

  for (xx = 1; xx < NSIG; xx++) {
    if (!st->hits[xx])
      continue;
    got = st->hits[xx];
    ev = Sigring[xx].next->self;
    while (ev) {
      queueEvent(ev, got);
      ev = ((pe_signal*)ev)->sring.next->self;
    }
    st->hits[xx] = 0;
  }
  Zero(st, 1, struct pe_sig_stat);
}

/* This implementation gives no race conditions! */
static void pe_signal_asynccheck()
{
  pe_sig_stat *st;

  st = &Sigstat[Sigslot];
  Sigslot = 1;
  if (st->Hits) _signal_asynccheck(st);

  st = &Sigstat[Sigslot];
  Sigslot = 0;
  if (st->Hits) _signal_asynccheck(st);
}


static void boot_signal()
{
  int xx;
  int sig;
  char **sigp;
  static char *keylist[] = {
    "signal"
  };
  static char *nohandle[] = {
    "KILL", "STOP", "ZERO", 0
  };
  pe_event_vtbl *vt = &pe_signal_vtbl;
  Zero(&Sigstat[0], 1, pe_sig_stat);
  Zero(&Sigstat[1], 1, pe_sig_stat);
  Sigslot = 0;
  for (xx=0; xx < NSIG; xx++) {
    PE_RING_INIT(&Sigring[xx], 0);
  }
  memset(Sigvalid, ~0, sizeof(Sigvalid));
  
  PE_SIGVALID_off(0);
  sigp = nohandle;
  while (*sigp) {
    sig = Perl_whichsig(*sigp);
    if (sig) PE_SIGVALID_off(sig);
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
  pe_register_vtbl(vt);
}

