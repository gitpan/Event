static struct pe_watcher_vtbl pe_io_vtbl;

static pe_ring IOWatch;
static int IOWatchCount;
static int IOWatch_OK;

static pe_watcher *pe_io_allocate()
{
  pe_io *ev;
  New(PE_NEWID, ev, 1, pe_io);
  ev->base.vtbl = &pe_io_vtbl;
  pe_watcher_init((pe_watcher*) ev);
  PE_RING_INIT(&ev->tm.ring, ev);
  PE_RING_INIT(&ev->ioring, ev);
  ev->fd = -1;
  ev->timeout = 0;
  ev->handle = 0;
  ev->events = 0;
  EvINVOKE1_off(ev);
  EvREPEAT_on(ev);
  return (pe_watcher*) ev;
}

static void pe_io_dtor(pe_watcher *_ev)
{
  pe_io *ev = (pe_io*) _ev;
  PE_RING_DETACH(&ev->ioring);
  if (ev->handle)
    SvREFCNT_dec(ev->handle);
  pe_watcher_dtor(_ev);
}

static void pe_io_start(pe_watcher *_ev, int repeat)
{
  pe_io *ev = (pe_io*) _ev;
  ev->fd = pe_sys_fileno(ev);
  PE_RING_UNSHIFT(&ev->ioring, &IOWatch);
  ++IOWatchCount;
  IOWatch_OK = 0;

  if (ev->timeout) {
    EvCBTIME_on(ev);
    ev->events |= PE_T;
    ev->tm.at = EvNOW(0) + ev->timeout;  /* too early okay */
    pe_timeable_start(&ev->tm);
  } else {
    EvCBTIME_off(ev);
    ev->events &= ~PE_T;
  }
}

static void pe_io_stop(pe_watcher *_ev)
{
  pe_io *ev = (pe_io*) _ev;
  pe_timeable_stop(&ev->tm);
  PE_RING_DETACH(&ev->ioring);
  --IOWatchCount;
  IOWatch_OK = 0;
}

static void pe_io_alarm(pe_watcher *_wa, pe_timeable *hit)
{
  pe_io *wa = (pe_io*) _wa;
  double now = EvNOW(1);
  double left = (_wa->cbtime + wa->timeout) - now;
  if (left < IntervalEpsilon) {
    pe_ioevent *ev;
    wa->tm.at = now + wa->timeout;
    ev = (pe_ioevent*) (*_wa->vtbl->new_event)(_wa);
    ++ev->base.count;
    ev->got |= PE_T;
    queueEvent((pe_event*) ev);
  }
  else {
    ++TimeoutTooEarly;
    wa->tm.at = now + left;
  }
  pe_timeable_start(&wa->tm);
}

static void _io_restart(pe_watcher *ev)
{
  if (!EvPOLLING(ev)) return;
  pe_watcher_off(ev);
  pe_watcher_on(ev, 0);
}

WKEYMETH(_io_events)
{
  pe_io *io = (pe_io*)ev;
  if (!nval) {
    dSP;
    XPUSHs(sv_2mortal(events_mask_2sv(io->events)));
    PUTBACK;
  } else {
    int nev = sv_2events_mask(nval, PE_R|PE_W|PE_E|PE_T);
    if (io->timeout) nev |=  PE_T;
    else             nev &= ~PE_T;
    if (io->events != nev) {
      io->events = nev;
      _io_restart(ev);
    }
  }
}

WKEYMETH(_io_handle)
{
  pe_io *io = (pe_io*)ev;
  if (!nval) {
    dSP;
    XPUSHs(io->handle? io->handle : &PL_sv_undef);
    PUTBACK;
  } else {
    SV *old = io->handle;
    io->handle = SvREFCNT_inc(nval);
    if (old) SvREFCNT_dec(old);
    _io_restart(ev);
  }
}

WKEYMETH(_io_timeout)
{
  pe_io *io = (pe_io*)ev;
  if (!nval) {
    dSP;
    XPUSHs(sv_2mortal(newSVnv(io->timeout)));
    PUTBACK;
  } else {
    io->timeout = SvNV(nval);
    _io_restart(ev);
  }
}

static void boot_io()
{
  pe_watcher_vtbl *vt = &pe_io_vtbl;
  memcpy(vt, &pe_watcher_base_vtbl, sizeof(pe_watcher_base_vtbl));
  vt->keymethod = newHVhv(vt->keymethod);
  hv_store(vt->keymethod, "events", 6, newSViv((IV)_io_events), 0);
  hv_store(vt->keymethod, "handle", 6, newSViv((IV)_io_handle), 0);
  hv_store(vt->keymethod, "timeout", 7, newSViv((IV)_io_timeout), 0);
  vt->dtor = pe_io_dtor;
  vt->start = pe_io_start;
  vt->stop = pe_io_stop;
  vt->alarm = pe_io_alarm;
  PE_RING_INIT(&IOWatch, 0);
  IOWatch_OK = 0;
  IOWatchCount = 0;
  pe_register_vtbl(vt, gv_stashpv("Event::io",1), &ioevent_vtbl);
}
