static struct pe_event_vtbl pe_timer_vtbl;

static pe_event *pe_timer_allocate()
{
  pe_timer *ev;
  New(PE_NEWID, ev, 1, pe_timer);
  assert(ev);
  ev->base.vtbl = &pe_timer_vtbl;
  PE_RING_INIT(&ev->tm.ring, ev);
  ev->tm.at = 0;
  ev->interval = &PL_sv_undef;
  pe_event_init((pe_event*) ev);
  return (pe_event*) ev;
}

static void pe_timer_dtor(pe_event *ev)
{
  pe_timer *tm = (pe_timer*) ev;
  SvREFCNT_dec(tm->interval);
  pe_event_dtor(ev);
}

static void pe_timer_start(pe_event *ev, int repeat)
{
  pe_timer *tm = (pe_timer*) ev;
  if (repeat) {
    /* We just finished the callback and need to re-insert at
       the appropriate time increment. */
    double interval;

    if (!sv_2interval(tm->interval, &interval))
      croak("Repeating timer with no interval");
    if (interval <= 0)
      croak("Timer has non-positive interval");

    tm->tm.at = interval + (EvHARD(ev)? tm->tm.at : EvNOW(1));
  }
  if (!tm->tm.at)
    croak("Timer unset");

  pe_timeable_start(&tm->tm);
}

static void pe_timer_stop(pe_event *ev)
{ pe_timeable_stop(&((pe_timer*)ev)->tm); }

static void pe_timer_alarm(pe_event *ev, pe_timeable *tm)
{ queueEvent(ev, 1); }

static void pe_timer_FETCH(pe_event *_ev, SV *svkey)
{
  pe_timer *ev = (pe_timer*) _ev;
  SV *ret=0;
  STRLEN len;
  char *key = SvPV(svkey, len);
  if (len && key[0] == '-') { ++key; --len; }
  if (!len) return;
  switch (key[0]) {
  case 'a':
    if (len == 2 && memEQ(key, "at", 2)) {
      ret = sv_2mortal(newSVnv(ev->tm.at));
      break;
    }
    break;
  case 'h':
    if (len == 4 && memEQ(key, "hard", 4)) {
      ret = boolSV(EvHARD(ev));
      break;
    }
    break;
  case 'w':
    if (len == 4 && memEQ(key, "when", 4)) {
      warn("Please use 'at' instead of 'when'");
      ret = sv_2mortal(newSVnv(ev->tm.at));
      break;
    }
    break;
  case 'i':
    if (len == 8 && memEQ(key, "interval", 8)) {
      ret = ev->interval;
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
pe_timer_STORE(pe_event *_ev, SV *svkey, SV *nval)
{
  pe_timer *ev = (pe_timer*) _ev;
  STRLEN len;
  char *key = SvPV(svkey, len);
  int ok=0;
  if (len && key[0] == '-') { ++key; --len; }
  if (!len) return;
  switch (key[0]) {
  case 'h':
    if (len == 4 && memEQ(key, "hard", 4)) {
      if (sv_true(nval))
	EvHARD_on(ev);
      else
	EvHARD_off(ev);
      ok=1;
      break;
    }
    break;
  case 'a':
    if (len == 2 && memEQ(key, "at", 2)) {
      int active = EvACTIVE(ev);
      ok=1;
      if (active)
	pe_timer_stop(_ev);
      ev->tm.at = SvNV(nval);
      if (active)
	pe_timer_start(_ev, 0);
      break;
    }
    break;
  case 'i':
    if (len == 8 && memEQ(key, "interval", 8)) {
      SV *old = ev->interval;
      ev->interval = SvREFCNT_inc(nval);
      SvREFCNT_dec(old);
      ok=1;
      break;
    }
    break;
  }
  if (!ok) (ev->base.vtbl->up->STORE)(_ev, svkey, nval);
}

static void boot_timer()
{
  static char *keylist[] = {
    "hard",
    "at",
    "interval"
  };
  pe_event_vtbl *vt = &pe_timer_vtbl;
/*
  SV *bt = perl_get_sv("^T", 0);
  if (SvNOK(bt)) {
    BaseTime = SvNV(bt);
  } else if (SvIOK(bt)) {
    BaseTime = SvIV(bt);
  }
*/
  memcpy(vt, &pe_event_base_vtbl, sizeof(pe_event_base_vtbl));
  vt->up = &pe_event_base_vtbl;
  vt->stash = (HV*) SvREFCNT_inc((SV*) gv_stashpv("Event::timer",1));
  vt->keys = sizeof(keylist)/sizeof(char*);
  vt->keylist = keylist;
  vt->dtor = pe_timer_dtor;
  vt->FETCH = pe_timer_FETCH;
  vt->STORE = pe_timer_STORE;
  vt->start = pe_timer_start;
  vt->stop = pe_timer_stop;
  vt->alarm = pe_timer_alarm;
  pe_register_vtbl(vt);
}
