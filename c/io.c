static struct pe_watcher_vtbl pe_io_vtbl;

static pe_ring IOWatch;
static int IOWatchCount;
static int IOWatch_OK;

static pe_watcher *pe_io_allocate(HV *stash) {
  pe_io *ev;
  New(PE_NEWID, ev, 1, pe_io);
  ev->base.vtbl = &pe_io_vtbl;
  pe_watcher_init(&ev->base, stash);
  PE_RING_INIT(&ev->tm.ring, ev);
  PE_RING_INIT(&ev->ioring, ev);
  ev->fd = -1;
  ev->timeout = 0;
  ev->handle = &PL_sv_undef;
  ev->poll = 0;
  EvINVOKE1_off(ev);
  EvREPEAT_on(ev);
  return (pe_watcher*) ev;
}

static void pe_io_dtor(pe_watcher *_ev)
{
  pe_io *ev = (pe_io*) _ev;
  PE_RING_DETACH(&ev->ioring);
  SvREFCNT_dec(ev->handle);
  pe_watcher_dtor(_ev);
}

static void pe_io_start(pe_watcher *_ev, int repeat)
{
    pe_io *ev = (pe_io*) _ev;
    if (SvOK(ev->handle)) {
	STRLEN n_a;
	ev->fd = pe_sys_fileno(ev->handle, SvPV(ev->base.desc, n_a));
    }
    /* On Unix, it is possible to set the 'fd' in C code without
       assigning anything to the 'handle'.  This should be supported
       somehow but maybe it is too unix specific? */
    if (ev->fd >= 0) {
	PE_RING_UNSHIFT(&ev->ioring, &IOWatch);
	++IOWatchCount;
	IOWatch_OK = 0;
    }
    if (ev->timeout) {
	EvCBTIME_on(ev);
	ev->poll |= PE_T;
	ev->tm.at = EvNOW(0) + ev->timeout;  /* too early okay */
	pe_timeable_start(&ev->tm);
    } else {
	EvCBTIME_off(ev);
	ev->poll &= ~PE_T;
    }
}

static void pe_io_stop(pe_watcher *_ev)
{
    pe_io *ev = (pe_io*) _ev;
    pe_timeable_stop(&ev->tm);
    if (!PE_RING_EMPTY(&ev->ioring)) {
	PE_RING_DETACH(&ev->ioring);
	--IOWatchCount;
	IOWatch_OK = 0;
    }
}

static void pe_io_alarm(pe_watcher *_wa, pe_timeable *hit)
{
    pe_io *wa = (pe_io*) _wa;
    double now = EvNOW(1);
    double left = (_wa->cbtime + wa->timeout) - now;
    if (left < IntervalEpsilon) {
	pe_ioevent *ev;
	if (EvREPEAT(wa)) {
	    wa->tm.at = now + wa->timeout;
	    pe_timeable_start(&wa->tm);
	} else {
	  wa->timeout = 0; /*RESET*/
	}
	ev = (pe_ioevent*) (*_wa->vtbl->new_event)(_wa);
	++ev->base.hits;
	ev->got |= PE_T;
	queueEvent((pe_event*) ev);
    }
    else {
	/* ++TimeoutTooEarly;
	   This branch is normal behavior and does not indicate
	   poor clock accuracy. */
	wa->tm.at = now + left;
	pe_timeable_start(&wa->tm);
    }
}

static void _io_restart(pe_watcher *ev)
{
    if (!EvPOLLING(ev)) return;
    pe_watcher_off(ev);
    pe_watcher_on(ev, 0);
}

WKEYMETH(_io_poll)
{
  pe_io *io = (pe_io*)ev;
  if (!nval) {
    dSP;
    XPUSHs(sv_2mortal(events_mask_2sv(io->poll)));
    PUTBACK;
  } else {
    int nev = sv_2events_mask(nval, PE_R|PE_W|PE_E|PE_T);
    if (io->timeout) nev |=  PE_T;
    else             nev &= ~PE_T;
    if (io->poll != nev) {
      io->poll = nev;
      _io_restart(ev);
    }
  }
}

WKEYMETH(_io_handle)
{
  pe_io *io = (pe_io*)ev;
  if (!nval) {
    dSP;
    XPUSHs(io->handle);
    PUTBACK;
  } else {
    SV *old = io->handle;
    io->handle = SvREFCNT_inc(nval);
    SvREFCNT_dec(old);
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
	io->timeout = SvOK(nval)? SvNV(nval) : 0;  /*undef is ok*/
	_io_restart(ev);
    }
}

static void boot_io()
{
  pe_watcher_vtbl *vt = &pe_io_vtbl;
  memcpy(vt, &pe_watcher_base_vtbl, sizeof(pe_watcher_base_vtbl));
  vt->keymethod = newHVhv(vt->keymethod);
  hv_store(vt->keymethod, "e_poll", 6, newSViv((IV)_io_poll), 0);
  hv_store(vt->keymethod, "e_fd", 4, newSViv((IV)_io_handle), 0);
  hv_store(vt->keymethod, "e_timeout", 9, newSViv((IV)_io_timeout), 0);
  vt->dtor = pe_io_dtor;
  vt->start = pe_io_start;
  vt->stop = pe_io_stop;
  vt->alarm = pe_io_alarm;
  PE_RING_INIT(&IOWatch, 0);
  IOWatch_OK = 0;
  IOWatchCount = 0;
  pe_register_vtbl(vt, gv_stashpv("Event::io",1), &ioevent_vtbl);
}
