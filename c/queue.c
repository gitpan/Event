static pe_ring NQueue;
static pe_stat idleStats;
static AV *Prepare, *Check, *AsyncCheck;
static int StarvePrio = PE_QUEUES - 2;
static double QueueTime[PE_QUEUES];

static void boot_queue()
{
  int xx;
  HV *stash = gv_stashpv("Event", 1);
  PE_RING_INIT(&NQueue, 0);
  memset(QueueTime, 0, sizeof(QueueTime));
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
  --ActiveWatchers;
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

static void prepare_event(pe_event *ev)
{
  assert(!EvSUSPEND(ev));
  assert(EvREENTRANT(ev) || !ev->running);
  if (!EvACTIVE(ev))
    croak("Event: attempt to run callback for !ACTIVE watcher '%s'",
	  SvPV(ev->desc,na));

  /* this cannot be done any later because the callback might want to
     again() or whatever */
  if (EvINVOKE1(ev) || (!EvINVOKE1(ev) && !EvREPEAT(ev)))
    pe_event_stop(ev);
}

static void queueEvent(pe_event *ev, int count)
{
  prepare_event(ev);

  assert(count >= 0);
  ev->count += count;

  if (ev->priority < 0) {  /* invoke the event immediately! */
    ev->priority = -1;
    if (EvDEBUGx(ev) >= 2)
      warn("Event: invoking %s (async)\n", SvPV(ev->desc,na));
    pe_event_invoke(ev);
    return;
  }

  if (EvQUEUED(ev))
    return;
  if (ev->priority >= PE_QUEUES)
    ev->priority = PE_QUEUES-1;
  if (EvDEBUGx(ev) >= 3)
    warn("Event: queue '%s' prio=%d\n", SvPV(ev->desc,na), ev->priority);
  QueueTime[ev->priority] = EvNOW(0);
  {
    /* queue in reverse direction? XXX */ 
    /*  warn("-- adding 0x%x/%d\n", ev, prio); db_show_queue();/**/
    pe_ring *rg;
    rg = NQueue.next;
    while (rg->self && ((pe_event*)rg->self)->priority <= ev->priority)
      rg = rg->next;
    PE_RING_ADD_BEFORE(&ev->que, rg);
    /*  warn("=\n"); db_show_queue();/**/
    EvQUEUED_on(ev);
    ++ActiveWatchers;
  }
}

static double pe_map_prepare(double tm)
{
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
  return tm;
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

static int pe_empty_queue(maxprio)
{
  pe_event *ev = NQueue.next->self;
  if (ev && ev->priority < maxprio) {
    dequeEvent(ev);
    if (EvDEBUGx(ev) >= 2)
      warn("Event: invoking '%s' (prio %d)\n",
	   SvPV(ev->desc, na), ev->priority);
    pe_event_invoke(ev);
    return 1;
  }
  return 0;
}

/*inline*/ static void pe_multiplex(double tm)
{
  if (SvIVX(DebugLevel) >= 2) {
    warn("Event: multiplex %.4fs %s%s\n", tm,
	 PE_RING_EMPTY(&NQueue)?"":"QUEUE",
	 PE_RING_EMPTY(&Idle)?"":"IDLE");
  }
  if (!Stats)
    pe_sys_multiplex(tm);
  else {
    struct timeval start_tm, done_tm;
    gettimeofday(&start_tm, 0);
    pe_sys_multiplex(tm);
    gettimeofday(&done_tm, 0);
    pe_stat_record(&idleStats,
		   (done_tm.tv_sec-start_tm.tv_sec +
		    (done_tm.tv_usec-start_tm.tv_usec)/1000000.0));
  }
}

static void pe_queue_pending()
{
  double tm = 0;
  if (av_len(Prepare) >= 0) tm = pe_map_prepare(tm);

  pe_multiplex(0);

  pe_timeables_check();
  if (av_len(Check) >= 0) pe_map_check(Check);

  pe_signal_asynccheck();
  if (av_len(AsyncCheck) >= 0) pe_map_check(AsyncCheck);
}

static int one_event(double tm)
{
  pe_signal_asynccheck();
  if (av_len(AsyncCheck) >= 0) pe_map_check(AsyncCheck);

  if (pe_empty_queue(StarvePrio)) return 1;

  if (!PE_RING_EMPTY(&NQueue) || !PE_RING_EMPTY(&Idle)) {
    tm = 0;
  }
  else {
    double t1 = timeTillTimer();
    if (t1 < tm) tm = t1;
  }
  if (av_len(Prepare) >= 0) tm = pe_map_prepare(tm);

  pe_multiplex(tm);

  pe_timeables_check();
  if (av_len(Check) >= 0) pe_map_check(Check);

  if (tm) {
    pe_signal_asynccheck();
    if (av_len(AsyncCheck) >= 0) pe_map_check(AsyncCheck);
  }

  if (pe_empty_queue(PE_QUEUES)) return 1;

  {
    pe_event *ev;
    if (PE_RING_EMPTY(&Idle)) return 0;
    PE_RING_POP(&Idle, ev);
    prepare_event(ev);
    /* can't queueEvent because we are already beyond that */
    ++ev->count;
    if (EvDEBUGx(ev) >= 2)
      warn("Event: invoking '%s' (idle)\n", SvPV(ev->desc, na));
    pe_event_invoke(ev);
    return 1;
  }
}

static int safe_one_event(double maxtm)
{
  pe_check_recovery();
  return one_event(maxtm);
}

static void pe_unloop(SV *why)
{
  SV *exitL = perl_get_sv("Event::ExitLevel", 0);
  SV *result = perl_get_sv("Event::Result", 0);
  assert(exitL && result);
  sv_setsv(result, why);
  sv_dec(exitL);
}
