static pe_ring NQueue;
static pe_ring Prepare, Check, AsyncCheck;
static pe_stat idleStats;
static int StarvePrio = PE_QUEUES - 2;
static double QueueTime[PE_QUEUES];

static void boot_queue()
{
  int xx;
  HV *stash = gv_stashpv("Event", 1);
  PE_RING_INIT(&NQueue, 0);
  PE_RING_INIT(&Prepare, 0);
  PE_RING_INIT(&Check, 0);
  PE_RING_INIT(&AsyncCheck, 0);
  memset(QueueTime, 0, sizeof(QueueTime));
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
  pe_watcher *wa = ev->up;
  if (EvSUSPEND(wa)) {
    EvRUNNOW_off(wa);
    (*ev->vtbl->dtor)(ev);
    return 0;
  }
  assert(EvREENTRANT(wa) || !wa->running);
  if (!EvACTIVE(wa)) {
    if (!EvRUNNOW(wa))
      warn("Event: event for !ACTIVE watcher '%s'", SvPV(wa->desc,PL_na));
  }
  else {
    if (!EvREPEAT(wa))
      pe_watcher_stop(wa);
    else if (EvINVOKE1(wa))
      pe_watcher_off(wa);
  }
  EvRUNNOW_off(wa); /* race condition XXX */
  if (EvDEBUGx(wa) >= 3)
    warn("Event: %s '%s' prio=%d\n", forwhat, SvPV(wa->desc,PL_na), ev->priority);
  return 1;
}

static void queueEvent(pe_event *ev)
{
  assert(ev->count);
  if (!PE_RING_EMPTY(&ev->que)) return; /* already queued */
  if (!prepare_event(ev, "queue")) return;

  if (ev->priority < 0) {  /* invoke the event immediately! */
    if (EvSUSPEND(ev->up)) {
      (*ev->vtbl->dtor)(ev);
    }
    else {
      ev->priority = -1;
      QueueTime[0] = EvNOW(0);
      pe_event_invoke(ev);
    }
    return;
  }
  if (ev->priority >= PE_QUEUES)
    ev->priority = PE_QUEUES-1;
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
    ++ActiveWatchers;
  }
}

static pe_qcallback *
pe_add_hook(char *which, int is_perl, void *cb, void *ext_data)
{
  pe_qcallback *qcb;
  New(PE_NEWID, qcb, 1, pe_qcallback);
  PE_RING_INIT(&qcb->ring, qcb);
  qcb->is_perl = is_perl;
  if (is_perl) {
    qcb->callback = SvREFCNT_inc((SV*)cb);
    qcb->ext_data = 0;
  }
  else {
    qcb->callback = cb;
    qcb->ext_data = ext_data;
  }
  if (strEQ(which, "prepare"))
    PE_RING_UNSHIFT(&qcb->ring, &Prepare);
  else if (strEQ(which, "check"))
    PE_RING_UNSHIFT(&qcb->ring, &Check);
  else if (strEQ(which, "asynccheck"))
    PE_RING_UNSHIFT(&qcb->ring, &AsyncCheck);
  else
    croak("Unknown hook '%s' in pe_add_hook", which);
  return qcb;
}

static pe_qcallback *capi_add_hook(char *which, void *cb, void *ext_data)
{ return pe_add_hook(which, 0, cb, ext_data); }

static void pe_cancel_hook(pe_qcallback *qcb)
{
  if (qcb->is_perl)
    SvREFCNT_dec((SV*)qcb->callback);
  PE_RING_DETACH(&qcb->ring);
  safefree(qcb);
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

static void pe_map_check(pe_ring *List)
{
  pe_qcallback *qcb = List->prev->self;
  while (qcb) {
    if (qcb->is_perl) {
      dSP;
      PUSHMARK(SP);
      PUTBACK;
      perl_call_sv((SV*)qcb->callback, G_DISCARD);
    }
    else { /* !is_perl */
      (* (void(*)(void*)) qcb->callback)(qcb->ext_data);
    }
    qcb = qcb->ring.prev->self;
  }
}

/* The caller is responsible for SAVETMPS/FREETMPS! */
static int pe_empty_queue(maxprio)
{
  pe_event *ev;
 RETRY:
  ev = NQueue.next->self;
  if (ev && ev->priority < maxprio) {
    dequeEvent(ev);
    if (EvSUSPEND(ev->up)) {
      (*ev->vtbl->dtor)(ev);
      goto RETRY;
    }
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
    if (EvSUSPEND(wa)) continue;
    /* nothing is queued so CLUMP will never be an option */
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
