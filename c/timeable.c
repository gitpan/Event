static pe_ring Timers;

typedef struct pe_tmevent pe_tmevent;
struct pe_tmevent {
  pe_event base;
  pe_timeable tm;
};

static void checkTimers()
{
  pe_ring *rg = Timers.next;
  while (rg->self && ((pe_tmevent*)rg->self)->tm.at < SvNVX(NowSV)) {
    pe_ring *nxt = rg->next;
    PE_RING_DETACH(rg);
    EvACTIVE_off(rg->self);
    queueEvent(rg->self, 1);
    rg = nxt;
  }
}

static double timeTillTimer()
{
  pe_ring *rg = Timers.next;
  if (!rg->self)
    return 3600;
  pe_cache_now();
  return ((pe_tmevent*) rg->self)->tm.at - SvNVX(NowSV);
}

static void pe_timeable_start(pe_event *ev)
{
  /* OPTIMIZE! */
  pe_tmevent *tm = (pe_tmevent*) ev;
  pe_ring *rg = Timers.next;
  while (rg->self && ((pe_tmevent*)rg->self)->tm.at < tm->tm.at) {
    rg = rg->next;
  }
  PE_RING_ADD_BEFORE(&tm->tm.ring, rg);
}

static void pe_timeable_stop(pe_event *ev)
{
  PE_RING_DETACH(&((pe_tmevent*)ev)->tm.ring);
}

void static boot_timeable()
{
  PE_RING_INIT(&Timers, 0);
}
