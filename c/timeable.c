static pe_timeable Timeables;

/*#define D_TIMEABLE(x) x /**/
#define D_TIMEABLE(x) /**/

/* BROQ
static void db_show_timeables()
{
  pe_tmevent *ev;
  ev = (pe_tmevent*) Timeables.next->self;
  while (ev) {
    warn("  %.2f : %s\n", ev->tm.at, SvPV(ev->base.desc, na));
    ev = (pe_tmevent*) ev->tm.ring.next->self;
  }
}
*/

static void pe_timeables_check()
{
  pe_timeable *tm = (pe_timeable*) Timeables.ring.next;
  double now = EvNOW(1);
  /*  warn("timeables at %.2f\n", now); db_show_timeables();/**/
  while (tm->ring.self && tm->at < now) {
    pe_event *ev = (pe_event*) tm->ring.self;
    pe_timeable *next = (pe_timeable*) tm->ring.next;
    D_TIMEABLE({
      if (EvDEBUGx(ev) >= 4)
	warn("Event: timeable expire '%s'\n", SvPV(ev->base.desc,na));
    })
    assert(!EvSUSPEND(ev));
    assert(EvACTIVE(ev));
    PE_RING_DETACH(&tm->ring);
    (*ev->vtbl->alarm)((pe_event*)ev, tm);
    tm = next;
  }
}

static double timeTillTimer()
{
  pe_timeable *tm = (pe_timeable*) Timeables.ring.next;
  if (!tm->ring.self)
    return 3600;
  return tm->at - EvNOW(1);
}

static void pe_timeable_start(pe_timeable *tm)
{
  /* OPTIMIZE! */
  pe_event *ev = (pe_event*) tm->ring.self;
  pe_timeable *rg = (pe_timeable*) Timeables.ring.next;
  assert(!EvSUSPEND(ev));
  assert(EvACTIVE(ev));
  assert(PE_RING_EMPTY(&tm->ring));
  while (rg->ring.self && rg->at < tm->at) {
    rg = (pe_timeable*) rg->ring.next;
  }
  /*warn("-- adding 0x%x:\n", ev); db_show_timeables();/**/
  PE_RING_ADD_BEFORE(&tm->ring, &rg->ring);
  /*warn("T:\n"); db_show_timeables();/**/
  D_TIMEABLE({
    if (EvDEBUGx(ev) >= 4)
      warn("Event: timeable start '%s'\n", SvPV(ev->desc,na));
  })
}

static void pe_timeable_stop(pe_timeable *tm)
{
  D_TIMEABLE({
    pe_event *ev = (pe_event*) tm->ring.self;
    if (EvDEBUGx(ev) >= 4)
      warn("Event: timeable stop '%s'\n", SvPV(ev->desc,na));
  })
  PE_RING_DETACH(&tm->ring);
}

void static boot_timeable()
{
  PE_RING_INIT(&Timeables.ring, 0);
}
