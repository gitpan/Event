static struct pe_event_vtbl pe_idle_vtbl;
static pe_ring Idle;

/* We can share ev->que because idle events are never queued and
   at the same time waiting to idle. */

static pe_event *
pe_idle_allocate()
{
  pe_event *ev;
  New(PE_NEWID, ev, 1, pe_event);
  ev->vtbl = &pe_idle_vtbl;
  pe_event_init(ev);
  return ev;
}

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

static void pe_idle_start(pe_event *ev, int repeat)
{
  if (EvACTIVE(ev) || EvQUEUED(ev) || EvSUSPEND(ev))
    return;
  if (EvDEBUG(ev))
    warn("Event: '%s' %s", SvPV(ev->desc,na), repeat? "again":"start");
  if (!PE_RING_EMPTY(&ev->que)) {  /* how does this happen? XXX */
    if (SvIVX(DebugLevel))
      warn("Event: idle '%s' queued unexpectedly", SvPV(ev->desc,na));
    /* harmless, but needs to be tracked down! */
    return;
  }
  EvACTIVE_on(ev);
  PE_RING_UNSHIFT(&ev->que, &Idle);
}

static void pe_idle_stop(pe_event *ev)
{
  if (PE_RING_EMPTY(&ev->que))
    return;
  if (EvDEBUG(ev))
    warn("Event: '%s' stop", SvPV(ev->desc,na));
  if (EvQUEUED(ev)) {
    /* perhaps via 'now' */
    dequeEvent(ev);
  }
  EvACTIVE_off(ev);
  PE_RING_DETACH(&ev->que);
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

