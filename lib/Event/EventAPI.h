#ifndef _event_api_H_
#define _event_api_H_

/*
  The API for the operating system dictates which events are
  truly asyncronous.  Event needs C-level support only for
  these types of events.
 */

typedef struct pe_watcher_vtbl pe_watcher_vtbl;
typedef struct pe_watcher pe_watcher;
typedef struct pe_event_vtbl pe_event_vtbl;
typedef struct pe_event pe_event;
typedef struct pe_ring pe_ring;

struct pe_ring { void *self; pe_ring *next, *prev; };

struct pe_watcher {
    pe_watcher_vtbl *vtbl;
    SV *mysv;
    double cbtime; /* float? XXX */
    void *callback;
    void *ext_data;
    void *stats;
    IV running; /* SAVEINT */
    U32 flags;
    SV *desc;
    pe_ring all;
    pe_ring events;  /* queued events */
    HV *FALLBACK;
    I16 event_counter; /* refcnt? XXX */
    I16 prio;
    I16 max_cb_tm;
};

struct pe_event {
    pe_event_vtbl *vtbl;
    SV *mysv;
    pe_watcher *up;
    pe_ring peer; /* homogeneous */
    pe_ring que;  /* heterogeneous */
    I16 hits;
    I16 prio;
};

/* This must be placed directly after pe_watcher so the memory
   layouts are always compatible. XXX? */
typedef struct pe_timeable pe_timeable;
struct pe_timeable {
    pe_ring ring;
    double at;
};

typedef struct pe_qcallback pe_qcallback;
struct pe_qcallback {
    pe_ring ring;
    int is_perl;
    void *callback;
    void *ext_data;
};

/* PUBLIC FLAGS */
#define PE_DEBUG	0x1000
#define PE_REPEAT	0x2000
#define PE_INVOKE1	0x4000
#define PE_CBTIME	0x8000

#define EvFLAGS(ev)		((pe_watcher*)ev)->flags

#define EvDEBUG(ev)		((EvFLAGS(ev) & PE_DEBUG)? 2:0) /*arthimetical*/
#define EvDEBUG_on(ev)		(EvFLAGS(ev) |= PE_DEBUG)
#define EvDEBUG_off(ev)		(EvFLAGS(ev) &= ~PE_DEBUG)

#define EvREPEAT(ev)		(EvFLAGS(ev) & PE_REPEAT)
#define EvREPEAT_on(ev)		(EvFLAGS(ev) |= PE_REPEAT)
#define EvREPEAT_off(ev)	(EvFLAGS(ev) &= ~PE_REPEAT)

#define EvINVOKE1(ev)		(EvFLAGS(ev) & PE_INVOKE1)
#define EvINVOKE1_on(ev)	(EvFLAGS(ev) |= PE_INVOKE1)
#define EvINVOKE1_off(ev)	(EvFLAGS(ev) &= ~PE_INVOKE1)

#define EvCBTIME(ev)		(EvFLAGS(ev) & PE_CBTIME)
#define EvCBTIME_on(ev)		(EvFLAGS(ev) |= PE_CBTIME)
#define EvCBTIME_off(ev)	(EvFLAGS(ev) &= ~PE_CBTIME)

/* QUEUE INFO */
#define PE_QUEUES 7	/* Hard to imagine a need for more than 7 queues... */
#define PE_PRIO_HIGH	2
#define PE_PRIO_NORMAL	4

/* io-ish flags */
#define PE_R 0x1
#define PE_W 0x2
#define PE_E 0x4
#define PE_T 0x8

typedef struct pe_ioevent pe_ioevent;
struct pe_ioevent {
    pe_event base;
    U16 got;
};

typedef struct pe_idle pe_idle;
struct pe_idle {
    pe_watcher base;
    pe_timeable tm;
    pe_ring iring;
    SV *max_interval, *min_interval;
};

typedef struct pe_io pe_io;
struct pe_io {
    pe_watcher base;
    pe_timeable tm; /*timeout*/
    pe_ring ioring;
    SV *handle;
    float timeout;
    U16 poll;
    /* ifdef UNIX */
    int fd;
    int xref;  /*private: for poll*/
    /* endif */
};

typedef struct pe_signal pe_signal;
struct pe_signal {
    pe_watcher base;
    pe_ring sring;
    IV signal;
};

typedef struct pe_timer pe_timer;
struct pe_timer {
    pe_watcher base;
    pe_timeable tm;
    SV *interval;
};

typedef struct pe_var pe_var;
struct pe_var {
    pe_watcher base;
    SV *variable;
    U16 events;
};

typedef struct pe_event_stats_vtbl pe_event_stats_vtbl;
struct pe_event_stats_vtbl {
    int on;
    void*(*enter)(int frame, int max_tm);
    void (*suspend)(void *);
    void (*resume)(void *);
    void (*commit)(void *, pe_watcher *);
    void (*scrub)(void *, pe_watcher *);
    void (*dtor)(void *);
};

struct EventAPI {
#define EventAPI_VERSION 21
    I32 Ver;

    /* EVENTS */
    void (*queue   )(pe_event *ev);
    void (*start   )(pe_watcher *ev, int repeat);
    void (*now     )(pe_watcher *ev);
    void (*stop    )(pe_watcher *ev, int cancel_events);
    void (*cancel  )(pe_watcher *ev);
    void (*suspend )(pe_watcher *ev);
    void (*resume  )(pe_watcher *ev);

    /* All constructors optionally take a stash and template.  Either
      or both can be NULL.  The template should not be a reference. */
    pe_idle     *(*new_idle  )(HV*, SV*);
    pe_timer    *(*new_timer )(HV*, SV*);
    pe_io       *(*new_io    )(HV*, SV*);
    pe_var      *(*new_var   )(HV*, SV*);
    pe_signal   *(*new_signal)(HV*, SV*);

    /* TIMEABLE */
    void (*tstart)(pe_timeable *);
    void (*tstop)(pe_timeable *);

    /* HOOKS */
    pe_qcallback *(*add_hook)(char *which, void *cb, void *ext_data);
    void (*cancel_hook)(pe_qcallback *qcb);

    /* STATS */
    void (*install_stats)(pe_event_stats_vtbl *esvtbl);
    void (*collect_stats)(int yes);
    pe_ring *AllWatchers;

    /* TYPEMAP */
    SV   *(*watcher_2sv)(pe_watcher *wa);
    void *(*sv_2watcher)(SV *sv);
    SV   *(*event_2sv)(pe_event *ev);
    void *(*sv_2event)(SV *sv);
};

static struct EventAPI *GEventAPI=0;

#define I_EVENT_API(YourName)						   \
STMT_START {								   \
  SV *sv = perl_get_sv("Event::API",0);					   \
  if (!sv) croak("Event::API not found");				   \
  GEventAPI = (struct EventAPI*) SvIV(sv);				   \
  if (GEventAPI->Ver != EventAPI_VERSION) {				   \
    croak("Event::API version mismatch (%d != %d) -- please recompile %s", \
	  GEventAPI->Ver, EventAPI_VERSION, YourName);			   \
  }									   \
} STMT_END

#endif
