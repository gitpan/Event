static pe_ring NQueue;
static pe_stat idleStats;
static AV *Prepare, *Check, *AsyncCheck;
static int StarvePrio = PE_QUEUES - 2;

static void boot_queue()
{
  int xx;
  HV *stash = gv_stashpv("Event", 1);
  PE_RING_INIT(&NQueue, 0);
  newCONSTSUB(stash, "QUEUES", newSViv(PE_QUEUES));
  newCONSTSUB(stash, "PRIO_NORMAL", newSViv(PE_PRIO_NORMAL));
  newCONSTSUB(stash, "PRIO_HIGH", newSViv(PE_PRIO_HIGH));

  Prepare = perl_get_av("Event::Prepare", 0);
  assert(Prepare);
  SvREFCNT_inc(Prepare);
  AsyncCheck = perl_get_av("Event::AsyncCheck", 0);
  assert(AsyncCheck);
  SvREFCNT_inc(AsyncCheck);
  Check = perl_get_av("Event::Check", 0);
  assert(Check);
  SvREFCNT_inc(Check);
}

static void dequeEvent(pe_event *ev)
{
  assert(ev);
  assert(!EvSUSPEND(ev));
  PE_RING_DETACH(&ev->que);
  EvQUEUED_off(ev);
}

static void db_show_queue()
{
  pe_event *ev;
  ev = NQueue.next->self;
  while (ev) {
    warn("0x%x : %d\n", ev, ev->priority);
    ev = ev->que.next->self;
  }
}

static void queueEvent(pe_event *ev, int count)
{
  pe_ring *rg;
  int prio = ev->priority;
  int debug = SvIVX(DebugLevel) + EvDEBUG(ev);

  if (EvSUSPEND(ev)) return;

  assert(count >= 0);
  ev->count += count;

  if (prio < 0) {  /* invoke the event immediately! */
    if (debug >= 3)
      warn("Event: calling %s asyncronously (priority %d)\n",
	   SvPV(ev->desc,na), prio);
    pe_event_invoke(ev);
    return;
  }

  if (EvQUEUED(ev))
    return;
  if (prio >= PE_QUEUES)
    prio = PE_QUEUES-1;
  if (debug >= 3)
    warn("Event: queuing '%s' at priority %d\n", SvPV(ev->desc,na), prio);
  /*  warn("-- adding 0x%x/%d\n", ev, prio); db_show_queue();/**/
  rg = NQueue.next;
  while (rg->self && ((pe_event*)rg->self)->priority <= prio)
    rg = rg->next;
  PE_RING_ADD_BEFORE(&ev->que, rg);
  /*  warn("=\n"); db_show_queue();/**/
  EvQUEUED_on(ev);
}

static void pe_map_check(AV *av)
{
  int xx;
  ENTER;
  SAVETMPS;
  for (xx=0; xx <= av_len(av); xx++) {
    SV **cv = av_fetch(av, xx, 0);
    dSP;
    PUSHMARK(SP);
    PUTBACK;
    if (!cv) croak("$AV[xx] unset");
    perl_call_sv(*cv, G_DISCARD);
  }
  FREETMPS;
  LEAVE;
}

/*
  recover if exited via longjmp

  @AsyncCheck

  return 1 if emptyQueue(QUEUES-2)

  tm = min @Prepare

  pe_io_waitForEvent(tm)

  @Check

  if (tm) @AsyncCheck

  return 1 if emptyQueue(QUEUES)

  return 1 runIdle

  return 0
 */

#define EMPTYQUEUE(max)				\
STMT_START {					\
  pe_event *ev = NQueue.next->self;		\
  if (ev && ev->priority < max) {		\
    dequeEvent(ev);				\
    pe_event_invoke(ev);			\
    return 1;					\
  }						\
} STMT_END

static int one_event(double tm)
{
  pe_signal_asynccheck();
  if (av_len(AsyncCheck) >= 0) pe_map_check(AsyncCheck);

  EMPTYQUEUE(StarvePrio);

  if (!PE_RING_EMPTY(&NQueue) || wantIdle()) {
    tm = 0;
  }
  else {
    double t1 = timeTillTimer();
    if (t1 < tm) tm = t1;
  }
  if (av_len(Prepare) >= 0) {
    /* untested XXX */
    int xx;
    ENTER;
    SAVETMPS;
    for (xx=0; xx <= av_len(Prepare); xx++) {
      SV *got;
      SV **cv = av_fetch(Prepare, xx, 0);
      dSP;
      PUSHMARK(SP);
      PUTBACK;
      if (!cv) croak("$Prepare[xx] unset");
      perl_call_sv(*cv, G_SCALAR);
      SPAGAIN;
      got = POPs;
      PUTBACK;
      if (SvOK(got) && SvNOK(got)) {
	double when = SvNV(got);
	if (when < tm) tm = when;
      }
    }
    FREETMPS;
    LEAVE;
  }

  if (SvIVX(DebugLevel) >= 2)
    warn("Event: waitForEvent(%f) wantIdle=%d\n", tm, wantIdle());
  {
    struct timeval start_tm;
    if (Stats)
      gettimeofday(&start_tm, 0);
    pe_io_waitForEvent(tm);
    if (Stats) {
      /* not strictly accurate, but close enough for government work */
      struct timeval done_tm;
      gettimeofday(&done_tm, 0);
      pe_stat_record(&idleStats, (done_tm.tv_sec-start_tm.tv_sec +
				  (done_tm.tv_usec-start_tm.tv_usec)/1000000.0));
    }
  }

  pe_timeables_check();
  if (av_len(Check) >= 0) pe_map_check(Check);

  if (tm) {
    pe_signal_asynccheck();
    if (av_len(AsyncCheck) >= 0) pe_map_check(AsyncCheck);
  }

  EMPTYQUEUE(PE_QUEUES);
  
  if (runIdle())
    return 1;

  return 0;
}

static int safe_one_event(double maxtm)
{
  pe_check_recovery();
  return one_event(maxtm);
}

