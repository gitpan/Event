static struct pe_event_vtbl pe_idle_vtbl;
static pe_ring Idle;

static pe_event *pe_idle_allocate()
{
  pe_idle *ev;
  New(PE_NEWID, ev, 1, pe_idle);
  ev->base.vtbl = &pe_idle_vtbl;
  pe_event_init((pe_event*) ev);
  PE_RING_INIT(&ev->tm.ring, ev);
  ev->tm.at = 0;
  PE_RING_INIT(&ev->iring, ev);
  return (pe_event*) ev;
}

static void pe_idle_start(pe_event *ev, int repeat)
{
  pe_idle *ip = (pe_idle*) ev;
  if (EvACTIVE(ev) || EvSUSPEND(ev))
    return;
  EvACTIVE_on(ev);
  PE_RING_UNSHIFT(&ip->iring, &Idle);
}

static void pe_idle_stop(pe_event *ev)
{
  pe_idle *ip = (pe_idle*) ev;
  if (!EvACTIVE(ev) || EvSUSPEND(ev))
    return;
  EvACTIVE_off(ev);
  PE_RING_DETACH(&ip->iring);
}

static void boot_idle()
{
  pe_event_vtbl *vt = &pe_idle_vtbl;
  PE_RING_INIT(&Idle, 0);
  memcpy(vt, &pe_event_base_vtbl, sizeof(pe_event_base_vtbl));
  vt->up = &pe_event_base_vtbl;
  vt->keys = 0;
  vt->stash = (HV*) SvREFCNT_inc((SV*) gv_stashpv("Event::idle",1));
  vt->start = pe_idle_start;
  vt->stop = pe_idle_stop;
  pe_register_vtbl(vt);
}

/********************************************/

static int runIdle()
{
  pe_event *ev;
  if (PE_RING_EMPTY(&Idle))
    return 0;
  PE_RING_POP(&Idle, ev);
  EvACTIVE_off(ev);
  ++ev->count;
  pe_event_invoke(ev);
  return 1;
}

static int wantIdle()
{ return !PE_RING_EMPTY(&Idle); }

