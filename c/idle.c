static struct pe_event_vtbl pe_idle_vtbl;
static pe_ring Idle;

/*#define D_IDLE(x) x  /**/
#define D_IDLE(x)  /**/

static pe_event *pe_idle_allocate()
{
  pe_idle *ev;
  New(PE_NEWID, ev, 1, pe_idle);
  ev->base.vtbl = &pe_idle_vtbl;
  pe_event_init((pe_event*) ev);
  PE_RING_INIT(&ev->tm.ring, ev);
  PE_RING_INIT(&ev->iring, ev);
  ev->max_interval = &sv_undef;
  ev->min_interval = newSVnv(.01);
  return (pe_event*) ev;
}

static void pe_idle_dtor(pe_event *ev)
{
  pe_idle *ip = (pe_idle*) ev;
  SvREFCNT_dec(ip->max_interval);
  SvREFCNT_dec(ip->min_interval);
  pe_event_dtor(ev);
}

static void pe_idle_start(pe_event *ev, int repeating)
{
  double now;
  double min,max;
  pe_idle *ip = (pe_idle*) ev;
  if (SvOK(ip->min_interval) || SvOK(ip->max_interval))
    EvCBTIME_on(ev);
  else
    EvCBTIME_off(ev);
  if (!repeating) ev->cbtime = EvNOW(1);
  now = EvHARD(ev)? ev->cbtime : EvNOW(1);
  if (sv_2interval(ip->min_interval, &min)) {
    ip->tm.at = min + now;
    pe_timeable_start(ev);
    D_IDLE(warn("min %.2f setup '%s'\n", min, SvPV(ev->desc,na)));
  }
  else {
    PE_RING_UNSHIFT(&ip->iring, &Idle);
    D_IDLE(warn("idle '%s'\n", SvPV(ev->desc,na)));
    if (sv_2interval(ip->max_interval, &max)) {
      D_IDLE(warn("max %.2f setup '%s'\n", max, SvPV(ev->desc,na)));
      ip->tm.at = max + now;
      pe_timeable_start(ev);
    }
  }
}

static void pe_idle_alarm(pe_event *ev)
{
  double now = EvNOW(1);
  double min,max,left;
  pe_idle *ip = (pe_idle*) ev;
  pe_timeable_stop(ev);
  if (sv_2interval(ip->min_interval, &min)) {
    left = ev->cbtime + min - now;
    if (left > PE_INTERVAL_EPSILON) {
      ip->tm.at = now + left;
      pe_timeable_start(ev);
      D_IDLE(warn("min %.2f '%s'\n", left, SvPV(ev->desc,na)));
      return;
    }
  }
  if (PE_RING_EMPTY(&ip->iring)) {
    PE_RING_UNSHIFT(&ip->iring, &Idle);
    D_IDLE(warn("idle '%s'\n", SvPV(ev->desc,na)));
  }
  if (sv_2interval(ip->max_interval, &max)) {
    left = ev->cbtime + max - now;
    if (left < PE_INTERVAL_EPSILON) {
      D_IDLE(warn("max '%s'\n", SvPV(ev->desc,na)));
      PE_RING_DETACH(&ip->iring);
      queueEvent(ev, 1);
      return;
    }
    else {
      ip->tm.at = now + left;
      D_IDLE(warn("max %.2f '%s'\n", left, SvPV(ev->desc,na)));
      pe_timeable_start(ev);
    }
  }
}

static void pe_idle_stop(pe_event *ev)
{
  pe_idle *ip = (pe_idle*) ev;
  PE_RING_DETACH(&ip->iring);
  pe_timeable_stop(ev);
}

static void pe_idle_FETCH(pe_event *_ev, SV *svkey)
{
  pe_idle *ev = (pe_idle*) _ev;
  SV *ret=0;
  STRLEN len;
  char *key = SvPV(svkey, len);
  if (len && key[0] == '-') { ++key; --len; }
  if (!len) return;
  switch (key[0]) {
  case 'h':
    if (len == 4 && memEQ(key, "hard", 4)) {
      ret = boolSV(EvHARD(ev));
      break;
    }
    break;
  case 'm':
    if (len == 12 && memEQ(key, "max_interval", 12)) {
      ret = ev->max_interval;
      break;
    }
    if (len == 12 && memEQ(key, "min_interval", 12)) {
      ret = ev->min_interval;
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

static void pe_idle_STORE(pe_event *_ev, SV *svkey, SV *nval)
{
  SV *old=0;
  pe_idle *ev = (pe_idle*)_ev;
  STRLEN len;
  char *key = SvPV(svkey, len);
  int ok=0;
  if (len && key[0] == '-') { ++key; --len; }
  if (!len) return;
  switch (key[0]) {
  case 'h':
    if (len == 4 && memEQ(key, "hard", 4)) {
      ok=1;
      if (sv_true(nval))
	EvHARD_on(ev);
      else
	EvHARD_off(ev);
      break;
    }
    break;
  case 'm':
    if (len == 12 && memEQ(key, "max_interval", 12)) {
      ok=1;
      old = ev->max_interval;
      ev->max_interval = SvREFCNT_inc(nval);
      break;
    }
    if (len == 12 && memEQ(key, "min_interval", 12)) {
      ok=1;
      old = ev->min_interval;
      ev->min_interval = SvREFCNT_inc(nval);
      break;
    }
    break;
  }
  if (ok) {
    if (old) SvREFCNT_dec(old);
    /* WILL ADAPT NEXT TIME THROUGH XXX?
    if (EvACTIVE(ev)) {
      pe_idle_stop(_ev);
      pe_idle_start(_ev, 0);
    }
    */
  }
  else
    (ev->base.vtbl->up->STORE)(_ev, svkey, nval);
}

static void boot_idle()
{
  static char *keylist[] = {
    "hard",
    "min_interval",
    "max_interval"
  };
  pe_event_vtbl *vt = &pe_idle_vtbl;
  PE_RING_INIT(&Idle, 0);
  memcpy(vt, &pe_event_base_vtbl, sizeof(pe_event_base_vtbl));
  vt->up = &pe_event_base_vtbl;
  vt->keys = sizeof(keylist)/sizeof(char*);
  vt->keylist = keylist;
  vt->stash = (HV*) SvREFCNT_inc((SV*) gv_stashpv("Event::idle",1));
  vt->dtor = pe_idle_dtor;
  vt->FETCH = pe_idle_FETCH;
  vt->STORE = pe_idle_STORE;
  vt->start = pe_idle_start;
  vt->stop = pe_idle_stop;
  vt->alarm = pe_idle_alarm;
  pe_register_vtbl(vt);
}
