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
  int run_id;
  int cbdone; /*can pack into flags XXX */
  int resume;
};

typedef struct pe_tied pe_tied;
struct pe_tied {
  pe_event base;
  pe_timeable tm;
};

struct pe_event_vtbl {
  /* how does it work for more than 1 level? XXX */
  /* only used for DELETE, EXISTS, FIRSTKEY, & NEXTKEY */
  struct pe_event_vtbl *up;

  int did_require;
  HV *stash;
  int keys;
  char **keylist;
  void (*dtor)(pe_event *);
  void (*FETCH)(pe_event *, SV *);
  void (*STORE)(pe_event *, SV *, SV *);
  void (*DELETE)(pe_event *, SV *key); /* never overridden? XXX */
  int (*EXISTS)(pe_event *, SV *key); /* never overridden? XXX */
  void (*FIRSTKEY)(pe_event *); /* never overridden? XXX */
  void (*NEXTKEY)(pe_event *); /* never overridden? XXX */
  void (*start)(pe_event *, int);
  void (*stop)(pe_event *);
  void (*alarm)(pe_event *, pe_timeable *);
  void (*postCB)(pe_cbframe *);
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

#define EvFLAGS(ev)		((pe_event*)ev)->flags
#define PE_ACTIVE	0x01
#define PE_SUSPEND	0x02
#define PE_QUEUED	0x04
#define PE_RUNNING	0x08  /* virtual flag */
#define PE_REENTRANT	0x10
#define PE_HARD		0x20
#define PE_PERLCB	0x40

#define PE_VISIBLE_FLAGS \
(PE_ACTIVE | PE_SUSPEND | PE_QUEUED | PE_RUNNING)

#define EvDEBUGx(ev) (SvIVX(DebugLevel) + EvDEBUG(ev))

/* ACTIVE: waiting for something to happen that might cause queueEvent */
/* controlled by start/stop methods */
#define EvACTIVE(ev)		(EvFLAGS(ev) & PE_ACTIVE)
#define EvACTIVE_on(ev)		(EvFLAGS(ev) |= PE_ACTIVE)
#define EvACTIVE_off(ev)	(EvFLAGS(ev) &= ~PE_ACTIVE)

#define EvSUSPEND(ev)		(EvFLAGS(ev) & PE_SUSPEND)
#define EvSUSPEND_on(ev)	(EvFLAGS(ev) |= PE_SUSPEND)
#define EvSUSPEND_off(ev)	(EvFLAGS(ev) &= ~PE_SUSPEND)

#define EvQUEUED(ev)		(EvFLAGS(ev) & PE_QUEUED)
#define EvQUEUED_on(ev)		(EvFLAGS(ev) |= PE_QUEUED)
#define EvQUEUED_off(ev)	(EvFLAGS(ev) &= ~PE_QUEUED)

#define EvREENTRANT(ev)		(EvFLAGS(ev) & PE_REENTRANT)
#define EvREENTRANT_on(ev)	(EvFLAGS(ev) |= PE_REENTRANT)
#define EvREENTRANT_off(ev)	(EvFLAGS(ev) &= ~PE_REENTRANT)

#define EvHARD(ev)		(EvFLAGS(ev) & PE_HARD)
#define EvHARD_on(ev)		(EvFLAGS(ev) |= PE_HARD)   /* :-) */
#define EvHARD_off(ev)		(EvFLAGS(ev) &= ~PE_HARD)

#define EvPERLCB(ev)		(EvFLAGS(ev) & PE_PERLCB)
#define EvPERLCB_on(ev)		(EvFLAGS(ev) |= PE_PERLCB)
#define EvPERLCB_off(ev)	(EvFLAGS(ev) &= ~PE_PERLCB)

#define EvCANDESTROY(ev)					\
 (ev->refcnt == 0 && ev->running == 0 &&			\
  !(EvFLAGS(ev)&(PE_ACTIVE|PE_SUSPEND|PE_QUEUED)))
