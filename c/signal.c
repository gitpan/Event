#if !defined(NSIG) || defined(M_UNIX) || defined(M_XENIX)
#include <signal.h>
#endif

static struct pe_watcher_vtbl pe_signal_vtbl;

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

static Signal_t process_sighandler(int sig) {
    pe_sig_stat *st = &Sigstat[Sigslot];
    ++st->Hits;
    ++st->hits[sig];
}

static pe_watcher *pe_signal_allocate(HV *stash, SV *temple) {
    pe_signal *ev;
    EvNew(5, ev, 1, pe_signal);
    ev->base.vtbl = &pe_signal_vtbl;
    PE_RING_INIT(&ev->sring, ev);
    ev->signal = 0;
    pe_watcher_init(&ev->base, stash, temple);
    EvREPEAT_on(ev);
    EvINVOKE1_off(ev);
    return (pe_watcher*) ev;
}

static void pe_signal_start(pe_watcher *_ev, int repeat) {
    pe_signal *ev = (pe_signal*) _ev;
    int sig = ev->signal;
    if (sig == 0)
	croak("No signal");
    if (PE_RING_EMPTY(&Sigring[sig]))
	rsignal(sig, process_sighandler);
    PE_RING_UNSHIFT(&ev->sring, &Sigring[sig]);
}

static void pe_signal_stop(pe_watcher *_ev) {
    pe_signal *ev = (pe_signal*) _ev;
    int sig = ev->signal;
    PE_RING_DETACH(&ev->sring);
    if (PE_RING_EMPTY(&Sigring[sig]))
	rsignal(sig, SIG_DFL);
}

WKEYMETH(_signal_signal) {
    pe_signal *sg = (pe_signal*) ev;
    if (!nval) {
	dSP;
	XPUSHs(sg->signal > 0?
	       sv_2mortal(newSVpv(PL_sig_name[sg->signal],0)) : &PL_sv_undef);
	PUTBACK;
    } else {
	STRLEN n_a;
	int active = EvPOLLING(ev);
	int sig = whichsig(SvPV(nval, n_a));
	/*warn("whichsig(%s) = %d", SvPV(nval,na), sig); /**/
	if (sig == 0)
	    croak("Unrecognized signal '%s'", SvPV(nval, n_a));
	if (!PE_SIGVALID(sig))
	    croak("Signal '%s' cannot be caught", SvPV(nval, n_a));
	if (active) pe_watcher_off(ev);
	sg->signal = sig;
	if (active) pe_watcher_on(ev, 0);
    }
}

static void _signal_asynccheck(pe_sig_stat *st) {
    int xx, got;
    pe_watcher *wa;

    for (xx = 1; xx < NSIG; xx++) {
	if (!st->hits[xx])
	    continue;
	got = st->hits[xx];
	wa = (pe_watcher*) Sigring[xx].next->self;
	while (wa) {
	    pe_event *ev = (*wa->vtbl->new_event)(wa);
	    ev->hits += got;
	    queueEvent(ev);
	    wa = (pe_watcher*) ((pe_signal*)wa)->sring.next->self;
	}
	st->hits[xx] = 0;
    }
    Zero(st, 1, struct pe_sig_stat);
}

/* This implementation gives no race conditions! */
static void pe_signal_asynccheck() {
    pe_sig_stat *st;

    st = &Sigstat[Sigslot];
    Sigslot = 1;
    if (st->Hits) _signal_asynccheck(st);

    st = &Sigstat[Sigslot];
    Sigslot = 0;
    if (st->Hits) _signal_asynccheck(st);
}


static void boot_signal() {
    int xx;
    int sig;
    char **sigp;
    /* is it perhaps a bit crufty to hardcode this list */
    static char *nohandle[] = { "KILL", "STOP", "ZERO", 0 };
    pe_watcher_vtbl *vt = &pe_signal_vtbl;
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
	sig = whichsig(*sigp);
	if (sig) PE_SIGVALID_off(sig);
	++sigp;
    }
    memcpy(vt, &pe_watcher_base_vtbl, sizeof(pe_watcher_base_vtbl));
    vt->start = pe_signal_start;
    vt->stop = pe_signal_stop;
    pe_register_vtbl(vt, gv_stashpv("Event::signal",1), &event_vtbl);
}
