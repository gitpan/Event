/* Hard to imagine a need for more than 7 queues... */
#define QUEUES 7
static pe_ring Queue[QUEUES];
static SV *queueCount;

static void
boot_queue()
{
  int xx;
  HV *stash = gv_stashpv("Event::Loop", 1);
  for (xx=0; xx < QUEUES; xx++) {
    PE_RING_INIT(&Queue[xx], 0);
  }
  queueCount = SvREFCNT_inc(perl_get_sv("Event::Loop::queueCount", 1));
  sv_setiv(queueCount, 0);
  SvREADONLY_on(queueCount);
  newCONSTSUB(stash, "QUEUES", newSViv(QUEUES));
  newCONSTSUB(stash, "PRIO_NORMAL", newSViv(PE_PRIO_NORMAL));
  newCONSTSUB(stash, "PRIO_HIGH", newSViv(PE_PRIO_HIGH));
}

static void
queueEvent(pe_event *ev, int count)
{
  int prio = ev->priority;
  int debug = SvIVX(DebugLevel) + EvDEBUG(ev);
  assert(count > 0);
  EvSUSPEND_off(ev);
  ev->count += count;
  if (prio < 0) {
    if (debug >= 3)
      warn("Event: calling %s asyncronously (priority %d)\n",
	   SvPV(ev->desc,na), prio);
    (*ev->vtbl->invoke)(ev);
    return;
  }
  if (EvQUEUED(ev))
    return;
  if (prio >= QUEUES)
    prio = QUEUES-1;
  if (debug >= 3)
    warn("Event: queuing %s at priority %d\n", SvPV(ev->desc,na), prio);
  PE_RING_UNSHIFT(&ev->que, &Queue[prio]);
  EvQUEUED_on(ev);
  ++SvIVX(queueCount);
}

static int
emptyQueue(int max)
{
  int qx;
  if (!SvIVX(queueCount))
    return 0;
  assert(max >= 0 && max <= QUEUES);
  for (qx=0; qx < max; qx++) {
    pe_event *ev;
    if (PE_RING_EMPTY(&Queue[qx]))
      continue;
    PE_RING_POP(&Queue[qx], ev);
    EvQUEUED_off(ev);
    --SvIVX(queueCount);
    (*ev->vtbl->invoke)(ev);
    return 1;
  }
  return 0;
}

/* extensible Qs: prepare, check, asynccheck  XXX */

static int
doOneEvent()
{
  double tm, timer;

  /* asynccheck */
  pe_signal_asynccheck();

  if (emptyQueue(QUEUES)) return 1;

  tm = SvIVX(queueCount) + wantIdle()? 0 : 60;
  /* prepare */
  timer = timeTillTimer();
  if (timer < tm) tm = timer;

  pe_io_waitForEvent(tm);

  /* check */
  checkTimers();

  if (tm) {
    /* asynccheck */
    pe_signal_asynccheck();
  }

  if (emptyQueue(QUEUES)) return 1;
  
  if (runIdle()) return 1;

  return 0;
}
