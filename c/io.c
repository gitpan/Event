static struct pe_event_vtbl pe_io_vtbl;

static pe_ring IOWatch;
static int IOWatchCount;
static int IOWatch_OK;

static pe_event *pe_io_allocate()
{
  pe_io *ev;
  New(PE_NEWID, ev, 1, pe_io);
  ev->base.vtbl = &pe_io_vtbl;
  pe_event_init((pe_event*) ev);
  PE_RING_INIT(&ev->tm.ring, ev);
  PE_RING_INIT(&ev->ttm.ring, ev);
  PE_RING_INIT(&ev->ioring, ev);
  ev->fd = -1;
  ev->timeout = 0;
  ev->tailpoll = 0;
  ev->handle = 0;
  ev->events = 0;
  ev->got = 0;
  EvINVOKE1_off(ev);
  EvREPEAT_on(ev);
  return (pe_event*) ev;
}

static void pe_io_dtor(pe_event *_ev)
{
  pe_io *ev = (pe_io*) _ev;
  PE_RING_DETACH(&ev->ioring);
  if (ev->handle)
    SvREFCNT_dec(ev->handle);
  (*_ev->vtbl->up->dtor)(_ev);
}

static void pe_io_start(pe_event *_ev, int repeat)
{
  struct stat sbuf;
  int statret;
  pe_io *ev = (pe_io*) _ev;
  ev->fd = pe_sys_fileno(ev);
  PE_RING_UNSHIFT(&ev->ioring, &IOWatch);
  ++IOWatchCount;
  IOWatch_OK = 0;

 retry:
  statret = fstat(ev->fd, &sbuf);
  if (statret && errno == EINTR) goto retry;

  if (!ev->tailpoll && ev->events & PE_R && S_ISREG(sbuf.st_mode))
    ev->tailpoll = 1;  /* hope okay to auto-detect */
  if (ev->tailpoll) {
    if (statret)
      croak("Event '%s': fstat(%d) failed with %d", SvPV(_ev->desc,PL_na),
	    ev->fd, errno);
    ev->size = sbuf.st_size;
    ev->ttm.at = EvNOW(0) + ev->tailpoll;
    pe_timeable_start(&ev->ttm);
  }

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

static void pe_io_stop(pe_event *_ev)
{
  pe_io *ev = (pe_io*) _ev;
  pe_timeable_stop(&ev->tm);
  pe_timeable_stop(&ev->ttm);
  PE_RING_DETACH(&ev->ioring);
  --IOWatchCount;
  IOWatch_OK = 0;
}

static void pe_io_alarm(pe_event *_ev, pe_timeable *hit)
{
  pe_io *ev = (pe_io*) _ev;
  double now = EvNOW(1);
  if (hit == &ev->tm) {
    double left = (_ev->cbtime + ev->timeout) - now;
    if (left < PE_INTERVAL_EPSILON) {
      ev->tm.at = now + ev->timeout;
      ev->got |= PE_T;
      queueEvent(_ev, 1);
    }
    else {
      ev->tm.at = left;
    }
    pe_timeable_start(&ev->tm);
  }
  else {
    struct stat sbuf;
    int ret;
  retry:
    ret = fstat(ev->fd, &sbuf);
    if (ret) {
      if (errno == EINTR) goto retry;
      croak("Event '%s': fstat(%d) failed with %d", SvPV(_ev->desc,PL_na),
	    ev->fd, errno);
    }
    if (sbuf.st_size != ev->size) {
      ev->size = sbuf.st_size;
      ev->got |= PE_R;
      queueEvent(_ev, 1);
    }
    ev->ttm.at = EvNOW(0) + ev->tailpoll;
    pe_timeable_start(&ev->ttm);
  }
}

static void pe_io_FETCH(pe_event *_ev, SV *svkey)
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
      ret = sv_2mortal(events_mask_2sv(ev->events));
      break;
    }
    break;
  case 'g':
    if (len == 3 && memEQ(key, "got", 3)) {
      ret = sv_2mortal(events_mask_2sv(ev->got));
      break;
    }
    break;
  case 'h':
    if (len == 6 && memEQ(key, "handle", 6)) {
      ret = ev->handle? ev->handle : &PL_sv_undef;
      break;
    }
    break;
  case 's':
    if (len == 4 && memEQ(key, "size", 4)) {
      ret = sv_2mortal(newSViv(ev->size));
      break;
    }
    break;
  case 't':
    if (len == 7 && memEQ(key, "timeout", 7)) {
      ret = sv_2mortal(newSVnv(ev->timeout));
      break;
    }
    if (len == 8 && memEQ(key, "tailpoll", 8)) {
      ret = sv_2mortal(newSVnv(ev->tailpoll));
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

static void pe_io_STORE(pe_event *_ev, SV *svkey, SV *nval)
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
      ok=1;
      ev->events = ev->timeout? PE_T : 0;
      if (SvPOK(nval)) {  /* can fall thru to SvIOK? XXX */
	ev->events |= sv_2events_mask(nval, PE_R|PE_W|PE_E);
#ifdef HAS_POLL
      } else if (SvIOK(nval)) {
	int mask;
	warn("please set events mask with a string");
	/* backward compatible support for POLL constants;
	   want to switch to our own constants! */
	mask = SvIV(nval);
	if (mask & (POLLIN | POLLRDNORM))
	  ev->events |= PE_R;
	if (mask & (POLLOUT | POLLWRNORM | POLLWRBAND))
	  ev->events |= PE_W;
	if (mask & (POLLRDBAND | POLLPRI))
	  ev->events |= PE_E;
#endif
      } else
	croak("Event::io::STORE('events'): expecting a string");
      break;
    }
    if (!(ev->events & PE_R))
      ev->tailpoll = 0;
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
      break;
    }
    break;
  case 's':
    if (len == 4 && memEQ(key, "size", 4))
      croak("'size' is read-only");
    break;
  case 't':
    if (len == 7 && memEQ(key, "timeout", 7)) {
      ok=1;
      ev->timeout = SvNV(nval);
      break;
    }
    if (len == 8 && memEQ(key, "tailpoll", 8)) {
      ok=1;
      ev->tailpoll = SvNV(nval);
      ev->events |= PE_R;
      break;
    }
    break;
  }
  if (ok) {
    if (EvACTIVE(ev)) {
      pe_io_stop(_ev);
      pe_io_start(_ev, 0);
    }
  }
  else
    (ev->base.vtbl->up->STORE)(_ev, svkey, nval);
}

static void pe_io_postCB(pe_cbframe *fp)
{
  pe_io *io = (pe_io*) fp->ev;
  io->got = 0;
  pe_event_postCB(fp);
}

static void boot_io()
{
  static char *keylist[] = {
    "handle",
    "events",
    "got",
    "timeout",
    "tailpoll",
    "size"
  };
  HV *stash = (HV*) SvREFCNT_inc((SV*) gv_stashpv("Event::io",1));
  pe_event_vtbl *vt = &pe_io_vtbl;
  memcpy(vt, &pe_event_base_vtbl, sizeof(pe_event_base_vtbl));
  vt->up = &pe_event_base_vtbl;
  vt->stash = stash;
  vt->keys = sizeof(keylist)/sizeof(char*);
  vt->keylist = keylist;
  vt->dtor = pe_io_dtor;
  vt->FETCH = pe_io_FETCH;
  vt->STORE = pe_io_STORE;
  vt->start = pe_io_start;
  vt->stop = pe_io_stop;
  vt->postCB = pe_io_postCB;
  vt->alarm = pe_io_alarm;
  PE_RING_INIT(&IOWatch, 0);
  IOWatch_OK = 0;
  IOWatchCount = 0;
  pe_register_vtbl(vt);
}
