static pe_ring Queue[PE_QUEUES];
static int queueCount = 0;
static pe_stat idleStats;
static AV *Prepare, *Check, *AsyncCheck;

static void
boot_queue()
{
  int xx;
  HV *stash = gv_stashpv("Event::Loop", 1);
  for (xx=0; xx < PE_QUEUES; xx++) {
    PE_RING_INIT(&Queue[xx], 0);
  }
  newCONSTSUB(stash, "QUEUES", newSViv(PE_QUEUES));
  newCONSTSUB(stash, "PRIO_NORMAL", newSViv(PE_PRIO_NORMAL));
  newCONSTSUB(stash, "PRIO_HIGH", newSViv(PE_PRIO_HIGH));

  Prepare = perl_get_av("Event::Loop::Prepare", 0);
  assert(Prepare);
  SvREFCNT_inc(Prepare);
  AsyncCheck = perl_get_av("Event::Loop::AsyncCheck", 0);
  assert(AsyncCheck);
  SvREFCNT_inc(AsyncCheck);
  Check = perl_get_av("Event::Loop::Check", 0);
  assert(Check);
  SvREFCNT_inc(Check);
}

static void dequeEvent(pe_event *ev)
{
  assert(ev);
  assert(!EvSUSPEND(ev));
  PE_RING_DETACH(&ev->que);
  EvQUEUED_off(ev);
  --queueCount;
}

static void
queueEvent(pe_event *ev, int count)
{
  int prio = ev->priority;
  int debug = SvIVX(DebugLevel) + EvDEBUG(ev);
  assert(count >= 0);
  ev->count += count;
  if (EvSUSPEND(ev))
    return;
  if (prio < 0) {
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
    warn("Event: queuing %s at priority %d\n", SvPV(ev->desc,na), prio);
  PE_RING_UNSHIFT(&ev->que, &Queue[prio]);
  EvQUEUED_on(ev);
  ++queueCount;
}

static int
emptyQueue(int max)
{
  int qx;
  if (!queueCount)
    return 0;
  assert(max >= 0 && max <= PE_QUEUES);
  for (qx=0; qx < max; qx++) {
    pe_event *ev;
    if (PE_RING_EMPTY(&Queue[qx]))
      continue;
    ev = Queue[qx].prev->self;
    dequeEvent(ev);
    pe_event_invoke(ev);
    return 1;
  }
  return 0;
}

static unsigned doe_enter=0, doe_leave=0;

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

static int
doOneEvent()
{
  int debug = SvIVX(DebugLevel);
  double tm, timer;

  if (doe_enter != doe_leave) {
    pe_event *ev;
    if (debug)
      warn("Event: exit via die detected\n");
    /* XXX do something more intelligent */
    ev = AllEvents.next->self;
    while (ev) {
      if (EvRUNNING(ev)) {
	if (debug) 
	  warn("Event: died in '%s'\n", SvPV(ev->desc,na));
	EvRUNNING_off(ev);
      }
      ev = ev->all.next->self;
    }
    if (debug) 
      warn("Event: trying to continue...\n");
    doe_enter = doe_leave = 0;
  }
  ++doe_enter;

  pe_signal_asynccheck();
  if (av_len(AsyncCheck) >= 0) pe_map_check(AsyncCheck);

  if (emptyQueue(PE_QUEUES-2)) {
    ++doe_leave;
    return 1;
  }

  tm = queueCount + wantIdle()? 0 : 60;
  timer = timeTillTimer();
  if (timer < tm) tm = timer;
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

  if (debug >= 3)
    warn("Event: waitForEvent(%f) wantIdle=%d", tm, wantIdle());
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

  checkTimers();
  if (av_len(Check) >= 0) pe_map_check(Check);

  if (tm) {
    pe_signal_asynccheck();
    if (av_len(AsyncCheck) >= 0) pe_map_check(AsyncCheck);
  }

  if (emptyQueue(PE_QUEUES)) {
    ++doe_leave;
    return 1;
  }
  
  if (runIdle()) {
    ++doe_leave;
    return 1;
  }

  ++doe_leave;
  return 0;
}
