static pe_ring Timeables;

/*#define D_TIMEABLE(x) x /**/
#define D_TIMEABLE(x) /**/

static void db_show_timeables()
{
  pe_tmevent *ev;
  ev = (pe_tmevent*) Timeables.next->self;
  while (ev) {
    warn("  %.2f : %s\n", ev->tm.at, SvPV(ev->base.desc, na));
    ev = (pe_tmevent*) ev->tm.ring.next->self;
  }
}

static void pe_timeables_check()
{
  pe_tmevent *ev = (pe_tmevent*) Timeables.next->self;
  double now = EvNOW(1);
  /*  warn("timeables at %.2f\n", now); db_show_timeables();/**/
  while (ev && ev->tm.at < now) {
    pe_tmevent *nev = (pe_tmevent*) ev->tm.ring.next->self;
    D_TIMEABLE({
      if (EvDEBUGx(ev) >= 4)
	warn("Event: timeable expire '%s'\n", SvPV(ev->base.desc,na));
    })
    assert(!EvSUSPEND(ev));
    assert(EvACTIVE(ev));
    PE_RING_DETACH(&ev->tm.ring);
    (*ev->base.vtbl->alarm)((pe_event*)ev);
    ev = nev;
  }
}

static double timeTillTimer()
{
  pe_ring *rg = Timeables.next;
  if (!rg->self)
    return 3600;
  return ((pe_tmevent*) rg->self)->tm.at - EvNOW(1);
}

static void pe_timeable_start(pe_event *ev)
{
  /* OPTIMIZE! */
  pe_tmevent *tm = (pe_tmevent*) ev;
  pe_ring *rg = Timeables.next;
  assert(!EvSUSPEND(ev));
  assert(EvACTIVE(ev));
  /* NOT okay to restart with stopping */
  assert(PE_RING_EMPTY(&((pe_tmevent*)ev)->tm.ring));
  while (rg->self && ((pe_tmevent*)rg->self)->tm.at < tm->tm.at) {
    rg = rg->next;
  }
  /*warn("-- adding 0x%x:\n", ev); db_show_timeables();/**/
  PE_RING_ADD_BEFORE(&tm->tm.ring, rg);
  /*warn("T:\n"); db_show_timeables();/**/
  D_TIMEABLE({
    if (EvDEBUGx(ev) >= 4)
      warn("Event: timeable start '%s'\n", SvPV(ev->desc,na));
  })
}

static void pe_timeable_stop(pe_event *ev)
{
  D_TIMEABLE({
    if (EvDEBUGx(ev) >= 4)
      warn("Event: timeable stop '%s'\n", SvPV(ev->desc,na));
  })
  PE_RING_DETACH(&((pe_tmevent*)ev)->tm.ring);
}

void static boot_timeable()
{
  PE_RING_INIT(&Timeables, 0);
}
