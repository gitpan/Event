static pe_ring Timeables;

typedef struct pe_tmevent pe_tmevent;
struct pe_tmevent {
  pe_event base;
  pe_timeable tm;
};

static void pe_timeables_check()
{
  pe_tmevent *ev = (pe_tmevent*) Timeables.next->self;
  double now = EvNOW(1);
  while (ev && ev->tm.at < now) {
    pe_tmevent *nev = (pe_tmevent*) ev->tm.ring.next->self;
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

static void db_show_timeables()
{
  pe_tmevent *ev;
  ev = (pe_tmevent*) Timeables.next->self;
  while (ev) {
    warn("0x%x : %.2f\n", ev, ev->tm.at);
    ev = (pe_tmevent*) ev->tm.ring.next->self;
  }
}

static void pe_timeable_start(pe_event *ev)
{
  /* OPTIMIZE! */
  pe_tmevent *tm = (pe_tmevent*) ev;
  pe_ring *rg = Timeables.next;
  while (rg->self && ((pe_tmevent*)rg->self)->tm.at < tm->tm.at) {
    rg = rg->next;
  }
  /*warn("-- adding 0x%x:\n", ev); db_show_timeables();/**/
  PE_RING_ADD_BEFORE(&tm->tm.ring, rg);
  /*warn("T:\n"); db_show_timeables();/**/
}

static void pe_timeable_stop(pe_event *ev)
{
  PE_RING_DETACH(&((pe_tmevent*)ev)->tm.ring);
}

void static boot_timeable()
{
  PE_RING_INIT(&Timeables, 0);
}
