
/* OS specific implementation of poll or select */
struct perl_poll {
  void (*set_min_time)(double tm);
  void (*block_on)(SV *handle, int flags);
  void (*do_wait)();
  void (*gotstuff)(SV *handle);
};

struct perl_event_source {
  HV *stash;					/* my perl type */
  int default_priority;
  void (*prepare)(struct perl_poll *, struct perl_event *);
  void (*check)(struct perl_poll *);

  /*INSTANCES*/
  struct perl_event_source *up;			/* inheritance */
  HV *default_stash;				/* instance stash */
  void (*ctor)(perl_event_base *);		/* init instance */
  void (*dtor)(perl_event_base *);		/* prepare to delete instance */
};

struct perl_event_link {
  struct perl_event_link *next,*prev;
};

/* base class for event source instances */
struct perl_event_base {
  perl_event_link link;		/* null or in queue */
  perl_event_source *vtbl;
  HV *stash;			/* my perl type */

  int refcnt;			/* ?maybe? */
  int priority_adjust;  	/* +-offset from default priority */
  int cancelled;
  int auto_repeat;		/* once only or auto repeat? */

  char *description;    	/* optional */
  void (*c_callback)();		/* args depend on event source type */
  void *xdata;          		/* for c_callback */
  SV *perl_callback;
  HV *more;			/* extra fields */
};

struct perl_event_io {
  perl_event_base base;
  SV *handle;
  int poll_flags;
};

struct perl_event_timer {
  perl_event_base base;
  int hard;		/* strict or sloppy repeat? */
  double when;
  double internval;
  /* should be implemented with a binary heap */
};

/* other event types can be implemented in perl */

struct perl_event_queue {
  perl_poll *pollobj;
  perl_event_source* sources[20];
  perl_event_link queue[16];
  int looplevel;		/* number of times loop is nested */
};

//DoOneEvent:
void queueDispatch(perl_event_queue *, int min_priority = 16);
void waitAndQueue(perl_event_queue *);
//void queueDispatch(perl_event_queue *, int min_priority = 16);
void idleDispatch(perl_event_queue *);
