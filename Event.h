#include "EventAPI.h"

#define PE_NEWID ('e'+'v')  /* for New() macro */

#define PE_RING_INIT(LNK, SELF) 		\
STMT_START {					\
  (LNK)->next = LNK;				\
  (LNK)->prev = LNK;				\
  (LNK)->self = SELF;				\
} STMT_END

#define PE_RING_EMPTY(LNK) ((LNK)->next == LNK)

#define PE_RING_UNSHIFT(LNK, ALL)		\
STMT_START {					\
  assert((LNK)->next==LNK);			\
  (LNK)->next = (ALL)->next;			\
  (LNK)->prev = ALL;				\
  (LNK)->next->prev = LNK;			\
  (LNK)->prev->next = LNK;			\
} STMT_END

#define PE_RING_ADD_BEFORE(L1,L2)		\
STMT_START {					\
  assert((L1)->next==L1);			\
  (L1)->next = L2;				\
  (L1)->prev = (L2)->prev;			\
  (L1)->next->prev = L1;			\
  (L1)->prev->next = L1;			\
} STMT_END

#define PE_RING_DETACH(LNK)			\
STMT_START {					\
  if ((LNK)->next != LNK) {			\
    (LNK)->next->prev = (LNK)->prev;		\
    (LNK)->prev->next = (LNK)->next;		\
    (LNK)->next = LNK;				\
  }						\
} STMT_END

#define PE_RING_POP(ALL, TO)			\
STMT_START {					\
  pe_ring *lk = (ALL)->prev;			\
  PE_RING_DETACH(lk);				\
  TO = lk->self;				\
} STMT_END

typedef struct pe_cbframe pe_cbframe;
struct pe_cbframe {
  pe_event *ev;
  IV run_id;
};

typedef struct pe_tied pe_tied;
struct pe_tied {
  pe_watcher base;
  pe_timeable tm;
};

#define WKEYMETH(M) static void M(pe_watcher *ev, SV *nval)
#define EKEYMETH(M) static void M(pe_event *ev, SV *nval)

typedef struct pe_base_vtbl pe_base_vtbl;
struct pe_base_vtbl {
  void (*Fetch)(void *, SV *key);
  void (*Store)(void *, SV *key, SV *nval);
  void (*Firstkey)(void *);
  void (*Nextkey)(void *);
};

struct pe_event_vtbl {  /* should be pure virtual XXX */
  pe_base_vtbl base;
  HV *keymethod;
  pe_event *(*new_event)(pe_watcher *);
  void (*dtor)(pe_event *);

  /* should be pure virtual XXX */
  pe_ring freelist;
};

struct pe_watcher_vtbl {
  pe_base_vtbl base;
  int did_require;
  HV *stash;
  HV *keymethod;
  void (*dtor)(pe_watcher *);
  void (*start)(pe_watcher *, int);
  void (*stop)(pe_watcher *);
  void (*alarm)(pe_watcher *, pe_timeable *);
  pe_event_vtbl *event_vtbl;
  pe_event *(*new_event)(pe_watcher *);
};

typedef struct pe_run pe_run;
struct pe_run {
  double elapse;
  int ran;
};

#define PE_STAT_SECONDS 3  /* 3 sec per interval */
#define PE_STAT_I1  20
#define PE_STAT_I2  20

struct pe_stat {
  int xsec, xmin;     /* first index of circular buffers */
  pe_run sec[PE_STAT_I1];
  pe_run min[PE_STAT_I2];
};
static void pe_stat_init(pe_stat *st);
static void pe_stat_record(pe_stat *st, double elapse);

#define EvFLAGS(ev)		((pe_watcher*)ev)->flags
#define PE_ACTIVE	0x001
#define PE_POLLING	0x002
#define PE_SUSPEND	0x004
#define PE_REENTRANT	0x008
#define PE_HARD		0x010
#define PE_PERLCB	0x020
#define PE_RUNNOW	0x040
#define PE_CLUMP	0x080
#define PE_QUEUED	0x100  /* virtual flag */
#define PE_RUNNING	0x200  /* virtual flag */

#define PE_VISIBLE_FLAGS \
(PE_ACTIVE | PE_SUSPEND | PE_QUEUED | PE_RUNNING)

#ifdef DEBUGGING
#  define EvDEBUGx(ev) (SvIV(DebugLevel) + EvDEBUG(ev))
#else
#  define EvDEBUGx(ev) 0
#endif

/* logically waiting for something to happen */
#define EvACTIVE(ev)		(EvFLAGS(ev) & PE_ACTIVE)
#define EvACTIVE_on(ev)		(EvFLAGS(ev) |= PE_ACTIVE)
#define EvACTIVE_off(ev)	(EvFLAGS(ev) &= ~PE_ACTIVE)

/* physically registered for poll/select */
#define EvPOLLING(ev)		(EvFLAGS(ev) & PE_POLLING)
#define EvPOLLING_on(ev)	(EvFLAGS(ev) |= PE_POLLING)
#define EvPOLLING_off(ev)	(EvFLAGS(ev) &= ~PE_POLLING)

#define EvSUSPEND(ev)		(EvFLAGS(ev) & PE_SUSPEND)
#define EvSUSPEND_on(ev)	(EvFLAGS(ev) |= PE_SUSPEND)
#define EvSUSPEND_off(ev)	(EvFLAGS(ev) &= ~PE_SUSPEND)

#define EvREENTRANT(ev)		(EvFLAGS(ev) & PE_REENTRANT)
#define EvREENTRANT_on(ev)	(EvFLAGS(ev) |= PE_REENTRANT)
#define EvREENTRANT_off(ev)	(EvFLAGS(ev) &= ~PE_REENTRANT)

#define EvHARD(ev)		(EvFLAGS(ev) & PE_HARD)
#define EvHARD_on(ev)		(EvFLAGS(ev) |= PE_HARD)   /* :-) */
#define EvHARD_off(ev)		(EvFLAGS(ev) &= ~PE_HARD)

#define EvPERLCB(ev)		(EvFLAGS(ev) & PE_PERLCB)
#define EvPERLCB_on(ev)		(EvFLAGS(ev) |= PE_PERLCB)
#define EvPERLCB_off(ev)	(EvFLAGS(ev) &= ~PE_PERLCB)

/* RUNNOW should be event specific XXX */
#define EvRUNNOW(ev)		(EvFLAGS(ev) & PE_RUNNOW)
#define EvRUNNOW_on(ev)		(EvFLAGS(ev) |= PE_RUNNOW)
#define EvRUNNOW_off(ev)	(EvFLAGS(ev) &= ~PE_RUNNOW)

#define EvCLUMP(ev)		(ev->ev1 && (EvFLAGS(ev) & PE_CLUMP))
#define EvCLUMP_on(ev)		(EvFLAGS(ev) |= PE_CLUMP)
#define EvCLUMP_off(ev)		(EvFLAGS(ev) &= ~PE_CLUMP)

#define EvCANDESTROY(ev)					\
 (ev->refcnt == 0 && ev->running == 0 &&			\
  !(EvFLAGS(ev)&(PE_ACTIVE|PE_SUSPEND|PE_QUEUED)))
