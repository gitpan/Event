/*
  Three Rings for the Elven-kings under the sky,
    Seven for the Dwarf-lords in their halls of stone,
  Nine for Mortal Men doomed to die,
    One for the Dark Lord on his dark throne
  In the Land of Mordor where the Shadows lie.
    One Ring to rule them all, One Ring to find them,
    One Ring to bring them all and in the darkness bind them
  In the Land of Mordor where the Shadows lie.

  s/ring/loop/ig; # :-)
 */

#ifndef _event_api_H_
#define _event_api_H_

/*
  There are a finite number of types of events that are truly
  asycronous.  Truly asycronous events must be carefully hand coded
  into the main Event distribution.  Listed below:
 */

typedef struct pe_event_vtbl pe_event_vtbl;
typedef struct pe_event pe_event;
typedef struct pe_stat pe_stat;
typedef struct pe_ring pe_ring;

struct pe_ring { void *self; pe_ring *next, *prev; };

struct pe_event {
  pe_event_vtbl *vtbl;
  HV *stash;
  U32 flags;
  I32 refcnt;
  SV *desc;
  pe_ring all, que;
  HV *FALLBACK;
  I16 iter;
  I16 id;
  I16 priority;
  IV running; /* SAVEINT */
  double cbtime;
  I32 count;
  void *callback;
  void *ext_data;
  pe_stat *stats;
};

/* This must be placed directly after pe_event so the memory
   layouts are always compatible. */
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

/* close enough to zero -- this needs to be bigger if you turn
   on lots of debugging?  Can determine clock resolution on the fly? XXX */
#define PE_INTERVAL_EPSILON 0.00001

/* PUBLIC FLAGS */
#define PE_DEBUG	0x0100
#define PE_REPEAT	0x0200
#define PE_INVOKE1	0x0400
#define PE_CBTIME	0x0800

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

typedef struct pe_idle pe_idle;
struct pe_idle {
  pe_event base;
  pe_timeable tm;
  pe_ring iring;
  SV *max_interval, *min_interval;
};

typedef struct pe_io pe_io;
struct pe_io {
  pe_event base;
  pe_timeable tm; /*timeout*/
  pe_timeable ttm; /*tailpoll*/
  pe_ring ioring;
  SV *handle;
  float timeout;
  float tailpoll;
  U16 events;
  U16 got;
/* ifdef UNIX */
  int fd;
  int xref;  /*private: for poll*/
  off_t size;
/* endif */
};

typedef struct pe_signal pe_signal;
struct pe_signal {
  pe_event base;
  pe_ring sring;
  IV signal;
};

typedef struct pe_timer pe_timer;
struct pe_timer {
  pe_event base;
  pe_timeable tm;
  SV *interval;
};

typedef struct pe_var pe_var;
struct pe_var {
  pe_event base;
  SV *variable;
  U16 events;
  U16 got;
};

struct EventAPI {
#define EventAPI_VERSION 9
  I32 Ver;

  /* EVENTS */
  void (*start)(pe_event *ev, int repeat);
  void (*queue)(pe_event *ev, int count);
  void (*now)(pe_event *ev);
  void (*suspend)(pe_event *ev);
  void (*resume)(pe_event *ev);
  void (*cancel)(pe_event *ev);

  pe_idle     *(*new_idle)();
  pe_timer    *(*new_timer)();
  pe_io       *(*new_io)();
  pe_var      *(*new_var)();
  pe_signal   *(*new_signal)();

  /* TIMEABLE */
  void (*tstart)(pe_timeable *);
  void (*tstop)(pe_timeable *);

  /* HOOKS */
  pe_qcallback *(*add_hook)(char *which, void *cb, void *ext_data);
  void (*cancel_hook)(pe_qcallback *qcb);
};

#define FETCH_EVENT_API(YourName, api)			\
STMT_START {						\
  SV *sv = perl_get_sv("Event::API",0);			\
  if (!sv) croak("Event::API not found");		\
  api = (struct EventAPI*) SvIV(sv);			\
  if (api->Ver != EventAPI_VERSION) {			\
    croak("Event::API version mismatch (%d != %d) -- you must recompile %s",	\
	  api->Ver, EventAPI_VERSION, YourName);	\
  }							\
} STMT_END

#endif
