static pe_ring NQueue;
static int StarvePrio = PE_QUEUES - 2;

static void boot_queue()
{
  int xx;
  HV *stash = gv_stashpv("Event", 1);
  PE_RING_INIT(&NQueue, 0);
  newCONSTSUB(stash, "QUEUES", newSViv(PE_QUEUES));
  newCONSTSUB(stash, "PRIO_NORMAL", newSViv(PE_PRIO_NORMAL));
  newCONSTSUB(stash, "PRIO_HIGH", newSViv(PE_PRIO_HIGH));
}

/*inline*/ static void dequeEvent(pe_event *ev)
{
  assert(ev);
  PE_RING_DETACH(&ev->que);
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

static int prepare_event(pe_event *ev, char *forwhat)
{
  /* AVOID DIEING IN HERE!! */
  STRLEN na;
  pe_watcher *wa = ev->up;
  assert(!EvSUSPEND(wa));
  assert(EvREENTRANT(wa) || !wa->running);
  if (!EvACTIVE(wa)) {
    if (!EvRUNNOW(wa))
      warn("Event: event for !ACTIVE watcher '%s'", SvPV(wa->desc,na));
  }
  else {
    if (!EvREPEAT(wa))
      pe_watcher_stop(wa, 0);
    else if (EvINVOKE1(wa))
      pe_watcher_off(wa);
  }
  EvRUNNOW_off(wa); /* race condition? XXX */
  if (EvDEBUGx(wa) >= 3)
    warn("Event: %s '%s' prio=%d\n", forwhat, SvPV(wa->desc,na), ev->priority);
  return 1;
}

static void queueEvent(pe_event *ev)
{
  assert(ev->count);
  if (!PE_RING_EMPTY(&ev->que)) return; /* already queued */
  if (!prepare_event(ev, "queue")) return;

  if (ev->priority < 0) {  /* invoke the event immediately! */
    ev->priority = 0;
    pe_event_invoke(ev);
    return;
  }
  if (ev->priority >= PE_QUEUES)
    ev->priority = PE_QUEUES-1;
  {
    /* queue in reverse direction? XXX */ 
    /*  warn("-- adding 0x%x/%d\n", ev, prio); db_show_queue();/**/
    pe_ring *rg;
    rg = NQueue.next;
    while (rg->self && ((pe_event*)rg->self)->priority <= ev->priority)
      rg = rg->next;
    PE_RING_ADD_BEFORE(&ev->que, rg);
    /*  warn("=\n"); db_show_queue();/**/
    ++ActiveWatchers;
  }
}

/* The caller is responsible for SAVETMPS/FREETMPS! */
static int pe_empty_queue(maxprio)
{
  pe_event *ev;
  ev = NQueue.next->self;
  if (ev && ev->priority < maxprio) {
    dequeEvent(ev);
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
  if (!Estat.on)
    pe_sys_multiplex(tm);
  else {
    void *st = Estat.enter(-1, 0);
    pe_sys_multiplex(tm);
    Estat.commit(st, 0);
  }
}

static double pe_map_prepare(double tm)
{
  pe_qcallback *qcb = Prepare.prev->self;
  while (qcb) {
    if (qcb->is_perl) {
      SV *got;
      double when;
      dSP;
      PUSHMARK(SP);
      PUTBACK;
      perl_call_sv((SV*)qcb->callback, G_SCALAR);
      SPAGAIN;
      got = POPs;
      PUTBACK;
      when = SvNOK(got) ? SvNVX(got) : SvNV(got);
      if (when < tm) tm = when;
    }
    else { /* !is_perl */
      double got = (* (double(*)(void*)) qcb->callback)(qcb->ext_data);
      if (got < tm) tm = got;
    }
    qcb = qcb->ring.prev->self;
  }
  return tm;
}

static void pe_queue_pending()
{
  double tm = 0;
  if (!PE_RING_EMPTY(&Prepare)) tm = pe_map_prepare(tm);

  pe_multiplex(0);

  pe_timeables_check();
  if (!PE_RING_EMPTY(&Check)) pe_map_check(&Check);

  pe_signal_asynccheck();
  if (!PE_RING_EMPTY(&AsyncCheck)) pe_map_check(&AsyncCheck);
}

/* The caller is responsible for SAVETMPS/FREETMPS! */
static int one_event(double tm)
{
  pe_signal_asynccheck();
  if (!PE_RING_EMPTY(&AsyncCheck)) pe_map_check(&AsyncCheck);

  if (pe_empty_queue(StarvePrio)) return 1;

  if (!PE_RING_EMPTY(&NQueue) || !PE_RING_EMPTY(&Idle)) {
    tm = 0;
  }
  else {
    double t1 = timeTillTimer();
    if (t1 < tm) tm = t1;
  }
  if (!PE_RING_EMPTY(&Prepare)) tm = pe_map_prepare(tm);

  pe_multiplex(tm);

  pe_timeables_check();
  if (!PE_RING_EMPTY(&Check)) pe_map_check(&Check);

  if (tm) {
    pe_signal_asynccheck();
    if (!PE_RING_EMPTY(&AsyncCheck)) pe_map_check(&AsyncCheck);
  }

  if (pe_empty_queue(PE_QUEUES)) return 1;

  while (1) {
    pe_watcher *wa;
    pe_event *ev;
    if (PE_RING_EMPTY(&Idle)) return 0;
    PE_RING_POP(&Idle, wa);
    /* idle is not an event so CLUMP is never an option but we still need
       to create an event to pass info to the callback */
    ev = pe_event_allocate(wa);
    if (!prepare_event(ev, "idle")) continue;
    /* can't queueEvent because we are already missed that */
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
