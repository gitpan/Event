static struct pe_event_vtbl pe_io_vtbl;

static pe_ring IOWatch;
static int IOWatchCount;
static int IOWatch_OK;

static pe_event *
pe_io_allocate()
{
  pe_io *ev;
  New(PE_NEWID, ev, 1, pe_io);
  ev->base.vtbl = &pe_io_vtbl;
  pe_event_init((pe_event*) ev);
  PE_RING_INIT(&ev->ioring, ev);
  ev->fd = -1;
  ev->handle = 0;
  ev->events = 0;
  ev->got = 0;
  EvINVOKE1_off(ev);
  EvREPEAT_on(ev);
  return (pe_event*) ev;
}

static void
pe_io_dtor(pe_event *_ev)
{
  pe_io *ev = (pe_io*) _ev;
  PE_RING_DETACH(&ev->ioring);
  if (ev->handle)
    SvREFCNT_dec(ev->handle);
  (*_ev->vtbl->up->dtor)(_ev);
}

static SV*
io_events_2sv(int mask)
{
  STRLEN len;
  SV *ret = newSV(0);
  SvUPGRADE(ret, SVt_PVIV);
  sv_setpvn(ret, "", 0);
  if (mask & PE_IO_R) sv_catpv(ret, "r");
  if (mask & PE_IO_W) sv_catpv(ret, "w");
  if (mask & PE_IO_E) sv_catpv(ret, "e");
  SvIVX(ret) = mask;
  SvIOK_on(ret);
  return ret;
}

static void
pe_io_FETCH(pe_event *_ev, SV *svkey)
{
  pe_io *ev = (pe_io*) _ev;
  SV *ret=0;
  STRLEN len;
  char *key = SvPV(svkey, len);
  if (len && key[0] == '-') { ++key; --len; }
  if (!len) return;
  switch (key[0]) {
  case 'e':
    if (len == 6 && memEQ(key, "events", 6)) {
      ret = sv_2mortal(io_events_2sv(ev->events));
      break;
    }
    break;
  case 'g':
    if (len == 3 && memEQ(key, "got", 3)) {
      ret = sv_2mortal(io_events_2sv(ev->got));
      break;
    }
    break;
  case 'h':
    if (len == 6 && memEQ(key, "handle", 6)) {
      ret = ev->handle? ev->handle : &sv_undef;
      break;
    }
    break;
  }
  if (ret) {
    dSP;
    XPUSHs(ret);
    PUTBACK;
  } else {
    (*ev->base.vtbl->up->FETCH)(_ev, svkey);
  }
}

static void
pe_io_STORE(pe_event *_ev, SV *svkey, SV *nval)
{
  pe_io *ev = (pe_io*) _ev;
  STRLEN len;
  char *key = SvPV(svkey, len);
  int ok=0;
  if (len && key[0] == '-') { ++key; --len; }
  if (!len) return;
  switch (key[0]) {
  case 'e':
    if (len == 6 && memEQ(key, "events", 6)) {
      int xx;
      ok=1;
      ev->events = 0;
      if (SvPOK(nval)) {  /* can fall thru to SvIOK? XXX */
	STRLEN el;
	char *ep = SvPV(nval,el);
	for (xx=0; xx < el; xx++) {
	  switch (ep[xx]) {
	  case 'r': ev->events |= PE_IO_R; break;
	  case 'w': ev->events |= PE_IO_W; break;
	  case 'e': ev->events |= PE_IO_E; break;
	  default: warn("ignored '%c'", ep[xx]);
	  }
	}
#ifdef HAS_POLL
      } else if (SvIOK(nval)) {
	/* backward compatible support for POLL constants */
	int mask = SvIV(nval);
	if (mask & (POLLIN | POLLRDNORM))
	  ev->events |= PE_IO_R;
	if (mask & (POLLOUT | POLLWRNORM | POLLWRBAND))
	  ev->events |= PE_IO_W;
	if (mask & (POLLRDBAND | POLLPRI))
	  ev->events |= PE_IO_E;
#endif
      } else
	croak("Event::io::STORE('events'): expecting a string");

      if (EvACTIVE(ev))
	IOWatch_OK = 0;
      break;
    }
    break;
  case 'g':
    if (len == 3 && memEQ(key, "got", 3))
      croak("The value of 'got' is set by the operating system");
    break;
  case 'h':
    if (len == 6 && memEQ(key, "handle", 6)) {
      ok=1;
      if (ev->handle)
	SvREFCNT_dec(ev->handle);
      ev->handle = SvREFCNT_inc(nval);
      if (EvACTIVE(ev))
	IOWatch_OK = 0;
      break;
    }
    break;
  }
  if (!ok) (ev->base.vtbl->up->STORE)(_ev, svkey, nval);
}

static void
pe_io_start(pe_event *_ev, int repeat)
{
  pe_io *ev = (pe_io*) _ev;
  if (EvACTIVE(ev) || EvSUSPEND(ev))
    return;
  PE_RING_UNSHIFT(&ev->ioring, &IOWatch);
  EvACTIVE_on(ev);
  ++IOWatchCount;
  IOWatch_OK = 0;
}

static void
pe_io_stop(pe_event *_ev)
{
  pe_io *ev = (pe_io*) _ev;
  if (!EvACTIVE(ev) || EvSUSPEND(ev))
    return;
  PE_RING_DETACH(&ev->ioring);
  EvACTIVE_off(ev);
  --IOWatchCount;
  IOWatch_OK = 0;
}

static void
boot_io()
{
  static char *keylist[] = {
    "handle",
    "events",
    "got"
  };
  pe_event_vtbl *vt = &pe_io_vtbl;
  memcpy(vt, &pe_event_base_vtbl, sizeof(pe_event_base_vtbl));
  vt->up = &pe_event_base_vtbl;
  vt->stash = (HV*) SvREFCNT_inc((SV*) gv_stashpv("Event::io",1));
  vt->keys = sizeof(keylist)/sizeof(char*);
  vt->keylist = keylist;
  vt->dtor = pe_io_dtor;
  vt->FETCH = pe_io_FETCH;
  vt->STORE = pe_io_STORE;
  vt->start = pe_io_start;
  vt->stop = pe_io_stop;
  PE_RING_INIT(&IOWatch, 0);
  IOWatch_OK = 0;
  IOWatchCount = 0;
  pe_register_vtbl(vt);
}
