#ifndef _event_api_H_
#define _event_api_H_

/*
  There are a finite number of types of events that are truly
  asycronous.  Truly asycronous events must be built into the
  main Event distribution.  Listed below:
 */

typedef struct pe_event_vtbl pe_event_vtbl;
typedef struct pe_event pe_event;
typedef struct pe_stat pe_stat;
typedef struct pe_ring pe_ring;

struct pe_ring { void *self; pe_ring *next, *prev; };

struct pe_event {
  pe_event_vtbl *vtbl;
  HV *stash;
  pe_ring all, que;
  int iter;
  HV *FALLBACK;
  int id;
  int refcnt;
  int flags;
  int priority;
  SV *desc;

  int count;           /* number of times queued */
  SV *perl_callback[2];
  void (*c_callback)();
  void *ext_data;

  pe_stat *stats;
};

/* PUBLIC FLAGS */
#define PE_DEBUG	0x10
#define PE_REPEAT	0x20
#define PE_INVOKE1	0x40

#define EvDEBUG(ev)		((EvFLAGS(ev) & PE_DEBUG)? 1:0) /*arthimetical*/
#define EvDEBUG_on(ev)		(EvFLAGS(ev) |= PE_DEBUG)
#define EvDEBUG_off(ev)		(EvFLAGS(ev) &= ~PE_DEBUG)

#define EvREPEAT(ev)		(EvFLAGS(ev) & PE_REPEAT)
#define EvREPEAT_on(ev)		(EvFLAGS(ev) |= PE_REPEAT)
#define EvREPEAT_off(ev)	(EvFLAGS(ev) &= ~PE_REPEAT)

#define EvINVOKE1(ev)		(EvFLAGS(ev) & PE_INVOKE1)
#define EvINVOKE1_on(ev)	(EvFLAGS(ev) |= PE_INVOKE1)
#define EvINVOKE1_off(ev)	(EvFLAGS(ev) &= ~PE_INVOKE1)

/* QUEUE INFO */
#define PE_QUEUES 7	/* Hard to imagine a need for more than 7 queues... */
#define PE_PRIO_HIGH	2
#define PE_PRIO_NORMAL	4

/* flags for pe_io::events */
#define PE_IO_R 1
#define PE_IO_W 2
#define PE_IO_E 4

typedef struct pe_io pe_io;
struct pe_io {
  pe_event base;
  pe_ring ioring;
  SV *handle;
  int events;
  int got;
/* ifdef UNIX */
  int fd;
  int xref;  /*private: for poll*/
/* endif */
};

typedef struct pe_signal pe_signal;
struct pe_signal {
  pe_event base;
  pe_ring sring;
  int sig;
};

typedef struct pe_timer pe_timer;
struct pe_timer {
  pe_event base;
  pe_ring tmring;
  int hard;
  double at;
  SV *interval;
};

typedef struct pe_watchvar pe_watchvar;
struct pe_watchvar {
  pe_event base;
  SV *variable;
};

struct EventAPI {
#define EventAPI_VERSION 0
  I32 Ver;

  /* PUBLIC API */
  int (*doOneEvent)();
  void (*start)(pe_event *ev, int repeat);
  void (*queue)(pe_event *ev, int count);
  void (*now)(pe_event *ev);
  void (*suspend)(pe_event *ev);
  void (*resume)(pe_event *ev);
  void (*cancel)(pe_event *ev);

  pe_event    *(*new_idle)();
  pe_timer    *(*new_timer)();
  pe_io       *(*new_io)();
  pe_watchvar *(*new_watchvar)();
  pe_signal   *(*new_signal)();
};

#define FETCH_EVENT_API(api)				\
STMT_START {						\
  SV *sv = perl_get_sv("Event::API",0);			\
  if (!sv) croak("Event::API not found");		\
  api = (struct EventAPI*) SvIV(sv);			\
  if (api->Ver != EventAPI_VERSION) {			\
    croak("Event::API version mismatch (%d != %d) -- you must recompile with the recently installed Event",	\
	  api->Ver, EventAPI_VERSION);			\
  }							\
} STMT_END

#endif
