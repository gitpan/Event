typedef struct pe_ring pe_ring;
struct pe_ring { void *self; pe_ring *next, *prev; };

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

typedef struct pe_event pe_event;
typedef struct pe_event_vtbl pe_event_vtbl;

struct pe_event_vtbl {
  struct pe_event_vtbl *up; /* dubious; how does it work for more than 1 level? */
  HV *stash;
  int keys;
  char **keylist;
  void (*init)(pe_event *);
  void (*dtor)(pe_event *);
  void (*FETCH)(pe_event *, SV *);
  void (*STORE)(pe_event *, SV *, SV *);
  void (*DELETE)(pe_event *, SV *key);
  int (*EXISTS)(pe_event *, SV *key);
  void (*FIRSTKEY)(pe_event *);
  void (*NEXTKEY)(pe_event *);
  void (*start)(pe_event *, int);
  void (*stop)(pe_event *);
};

typedef struct pe_run pe_run;
struct pe_run {
  double elapse;
  int ran;
};

#define PE_STAT_SECONDS 3  /* 3 sec per interval */
#define PE_STAT_I1  20
#define PE_STAT_I2  20

typedef struct pe_stat pe_stat;
struct pe_stat {
  int xsec, xmin;     /* first index of circular buffers */
  pe_run sec[PE_STAT_I1];
  pe_run min[PE_STAT_I2];
};
static void pe_stat_init(pe_stat *st);
static void pe_stat_record(pe_stat *st, double elapse);

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

  pe_stat stats;
};

#define EvFLAGS(ev)		((pe_event*)ev)->flags
#define PE_ACTIVE	0x1
#define PE_SUSPEND	0x2
#define PE_QUEUED	0x4
#define PE_RUNNING	0x8
#define PE_DEBUG	0x10
#define PE_REPEAT	0x20
#define PE_INVOKE1	0x40

#define PE_INTERNAL_FLAGS \
(PE_QUEUED | PE_SUSPEND | PE_RUNNING | PE_DEBUG | PE_REPEAT)

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

#define EvRUNNING(ev)		(EvFLAGS(ev) & PE_RUNNING)
#define EvRUNNING_on(ev)	(EvFLAGS(ev) |= PE_RUNNING)
#define EvRUNNING_off(ev)	(EvFLAGS(ev) &= ~PE_RUNNING)

#define EvDEBUG(ev)		((EvFLAGS(ev) & PE_DEBUG)? 1:0) /*arthimetic*/
#define EvDEBUG_on(ev)		(EvFLAGS(ev) |= PE_DEBUG)
#define EvDEBUG_off(ev)		(EvFLAGS(ev) &= ~PE_DEBUG)

#define EvREPEAT(ev)		(EvFLAGS(ev) & PE_REPEAT)
#define EvREPEAT_on(ev)		(EvFLAGS(ev) |= PE_REPEAT)
#define EvREPEAT_off(ev)	(EvFLAGS(ev) &= ~PE_REPEAT)

#define EvINVOKE1(ev)		(EvFLAGS(ev) & PE_INVOKE1)
#define EvINVOKE1_on(ev)	(EvFLAGS(ev) |= PE_INVOKE1)
#define EvINVOKE1_off(ev)	(EvFLAGS(ev) &= ~PE_INVOKE1)

#define EvCANDESTROY(ev)					\
 (ev->refcnt == 0 &&						\
  !(EvFLAGS(ev)&(PE_ACTIVE|PE_SUSPEND|PE_QUEUED|PE_RUNNING)) &&	\
  !ev->c_callback)

/* PUBLIC API */
                       /* PRELIMINARY! XXX */
void pe_event_cancel(pe_event *ev);
void pe_event_suspend(pe_event *ev);
void pe_event_now(pe_event *ev);

