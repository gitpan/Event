#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>
#if !defined(NSIG) || defined(M_UNIX) || defined(M_XENIX)
#include <signal.h>
#endif
#ifdef I_SYS_WAIT
#  include <sys/wait.h>
#endif

#define MAX_CHLD_SLOT 63
#define CHLD_BUF_SIZE (MAX_CHLD_SLOT+1)

struct child_data {
    int pid;
    int status;
};

static VOL int chld_next;
static struct child_data chld_buf[CHLD_BUF_SIZE];
static int signal_count[NSIG];

static Signal_t chld_sighandler _((int sig));

static void
initialize()
{
    chld_next=0;
    memzero(signal_count, sizeof(signal_count));
    memzero(chld_buf, sizeof(chld_buf));

    (void)rsignal(SIGCHLD, chld_sighandler);
}

static I32
tracevar(ix, sv)
IV ix;
SV *sv;
{
    dXSARGS;
    SV *obj = (SV *)ix;
    SV **cvptr;
    /* Taken From tkGlue.c

       We are a "magic" set processor, whether we like it or not
       because this is the hook we use to get called.
       So we are (I think) supposed to look at "private" flags 
       and set the public ones if appropriate.
       e.g. "chop" sets SvPOKp as a hint but not SvPOK

       presumably other operators set other private bits.

       Question are successive "magics" called in correct order?

       i.e. if we are tracing a tied variable should we call 
       some magic list or be careful how we insert ourselves in the list?

    */

    if (!SvPOK(sv) && SvPOKp(sv))
	SvPOK_on(sv);

    if (!SvNOK(sv) && SvNOKp(sv))
	SvNOK_on(sv);

    if (!SvIOK(sv) && SvIOKp(sv))
	SvIOK_on(sv);

    if (cvptr = hv_fetch((HV*)SvRV(obj),"callback",8,0)) {
	PUSHMARK(sp);
	XPUSHs(obj);
	XPUSHs(sv);
	PUTBACK;
	perl_call_sv(*cvptr,0);
    }
}

static void
var__unwatchvar(obj)
    SV * obj;
{
    MAGIC **mgp;
    MAGIC *mg;
    MAGIC *mgtmp;
    SV **svptr,*sv;

    svptr = hv_fetch((HV*)SvRV(obj),"variable",8,0);
    sv = SvRV(*svptr);

    if(!SvROK(*svptr) || (SvTYPE(sv) < SVt_PVMG) || !SvMAGIC(sv))
	return;

    mgp = &SvMAGIC(sv);
    while ((mg = *mgp)) {
	if(mg->mg_obj == obj)
	    break;
	mgp = &mg->mg_moremagic;
    }

    if(!mg)
	return;

    *mgp = mg->mg_moremagic;

    mgtmp = SvMAGIC(sv);
    SvMAGIC(sv) = mg;
    mg_free(sv);
    SvMAGIC(sv) = mgtmp;
}

static void
var__watchvar(obj)
    SV * obj;
{
#ifdef dTHR
    dTHR;
#endif
    struct ufuncs *ufp;
    MAGIC **mgp;
    MAGIC *mg;
    SV **svptr,*sv;

    svptr = hv_fetch((HV*)SvRV(obj),"variable",8,0);
    sv = SvRV(*svptr);

    if(!SvROK(*svptr))
	return;

    if (SvTHINKFIRST(sv)) {
	if (SvREADONLY(sv) && curcop != &compiling)
	    croak("Cannot trace readonly variable");
    }
    if (!SvUPGRADE(sv, SVt_PVMG))
	croak("Trace SvUPGRADE failed");

    mgp = &SvMAGIC(sv);
    while ((mg = *mgp)) {
	mgp = &mg->mg_moremagic;
    }

    Newz(702, mg, 1, MAGIC);
    mg->mg_moremagic = NULL;
    *mgp = mg;

    mg->mg_obj = SvREFCNT_inc(obj);
    mg->mg_flags |= MGf_REFCOUNTED;
    mg->mg_type = 'U';
    mg->mg_len = 0;
    mg->mg_virtual = &vtbl_uvar;

    mg_magical(sv);
    New(666, ufp, 1, struct ufuncs);
    ufp->uf_val = 0;
    ufp->uf_set = tracevar;
    ufp->uf_index = (IV) obj;
    mg->mg_ptr = (char *) ufp;

    if (!SvMAGICAL(sv))
	abort();

}

#ifdef WNOHANG
# ifdef HAS_WAITPID
#  define Wait4Any(s) waitpid(-1,(s),WNOHANG)
# else
#  ifdef HAS_WAIT4
#   define Wait4Any(s) wait4(-1,(s),WNOHANG,0)
#  endif
# endif
#endif

static Signal_t
chld_sighandler(sig)
int sig;
{
    int pid;
    int status;
    int slot = chld_next++;

    if(slot >= MAX_CHLD_SLOT)
	(void)rsignal(SIGCHLD, SIG_DFL);

#ifdef Wait4Any
    pid = Wait4Any(&status);
#else
    pid = wait(&status);
#endif
    chld_buf[slot].pid = pid;
    chld_buf[slot].status = status;

    if(chld_next < CHLD_BUF_SIZE)
	(void)rsignal(SIGCHLD, chld_sighandler);
}

static Signal_t
process_sighandler(sig)
int sig;
{
 signal_count[sig]++;
}

MODULE=Event	PACKAGE=Event::process

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

MODULE=Event	PACKAGE=Event::watchvar	PREFIX=var_

void
var__watchvar(obj)
    SV *	obj
PROTOTYPE: $

void
var__unwatchvar(obj)
    SV *	obj
PROTOTYPE: $

MODULE=Event	PACKAGE=Event::signal

void
RealSigName(sv)
    SV * sv
PROTOTYPE: $
PPCODE:
    char *s = SvPV(sv,na);
    if(s && *s == '_') {
	if(strNE(s,"__DIE__") && strNE(s,"__WARN__") && strNE(s,"__PARSE__"))
	    ST(0) = &sv_undef;
    }
    else {
	int sig = whichsig(s);
	if(sig) {
	    ST(0) = sv_newmortal();
	    sv_setpv(ST(0),sig_name[sig]);
	}
	else
	    ST(0) = &sv_undef;
    }
    XSRETURN(1);


void
_watch_signal(name)
    char *	name
PPCODE:
{
    int sig = whichsig(name);
    if(sig && (sig != SIGCHLD))
	rsignal(sig,process_sighandler);
    XSRETURN(0);
}

void
_unwatch_signal(name)
    char *	name
PPCODE:
{
    int sig = whichsig(name);
    if(sig && (sig != SIGCHLD))
	rsignal(sig,SIG_DFL);
    XSRETURN(0);
}

void
_reap()
PPCODE:
{
    int i,count,val;
    count = 0;
    for(i = 1 ; i < NSIG ; i++) {
	if((val = signal_count[i]) != 0) {
	    signal_count[i] = 0;
	    EXTEND(sp,2);
	    count += 2;
	    XPUSHs(sv_2mortal(newSVpv(sig_name[i],0)));
	    XPUSHs(sv_2mortal(newSViv(val)));
	}
    }
    XSRETURN(count);
}


BOOT:
	initialize(); 
