static struct pe_event_vtbl pe_timer_vtbl;
static pe_ring Timers;
/* static double BaseTime; /* make everything relative? XXX */
static double Now; /* EXPORT XXX */

typedef struct pe_timer pe_timer;
struct pe_timer {
  pe_event base;
  pe_ring tmring;
  int hard;
  double at;
  double interval;
};

static pe_event *
pe_timer_allocate()
{
  pe_timer *ev;
  New(PE_NEWID, ev, 1, pe_timer);
  ev->base.vtbl = &pe_timer_vtbl;
  PE_RING_INIT(&ev->tmring, ev);
  ev->hard = 0;
  ev->at = 0;
  ev->interval = 0;
  pe_event_init((pe_event*) ev);
  return (pe_event*) ev;
}

static void
cacheNow()
{
  struct timeval now_tm;
  gettimeofday(&now_tm, 0);
  Now = now_tm.tv_sec + now_tm.tv_usec / 1000000.0;
}

static void
checkTimers()
{
  pe_ring *rg = Timers.next;
  while (rg->self && ((pe_timer*)rg->self)->at < Now) {
    pe_ring *nxt = rg->next;
    PE_RING_DETACH(rg);
    EvACTIVE_off(rg->self);
    queueEvent(rg->self, 1);
    rg = nxt;
  }
}

static double
timeTillTimer()
{
  pe_ring *rg = Timers.next;
  if (!rg->self)
    return 3600;
  cacheNow();
  return ((pe_timer*) rg->self)->at - Now;
}

static void
pe_timer_start(pe_event *ev, int repeat)
{
  pe_timer *tm = (pe_timer*) ev;
  pe_ring *rg;
  EvSUSPEND_off(ev);
  if (EvACTIVE(ev))
    return;
  if (repeat && EvREPEAT(ev)) {
    /* We just finished the callback and need to re-insert at
       the appropriate time increment. */
    if (!tm->interval)
      croak("Timer has no interval");
    if (tm->hard) {
      tm->at = tm->interval + tm->at;
    } else {
      cacheNow();
      tm->at = tm->interval + Now;
    }
  }
  if (!tm->at)
    croak("Timer unset");
  /* Binary insertion sort?  No.  Long-term timers deserve the
     performance hit.  Code should be optimal for short-term
     timers. */
  rg = Timers.next;
  while (rg->self && ((pe_timer*)rg->self)->at < tm->at) {
    rg = rg->next;
  }
  PE_RING_ADD_BEFORE(&tm->tmring, rg);
  EvACTIVE_on(ev);
}

static void
pe_timer_stop(pe_event *ev)
{
  pe_timer *tm = (pe_timer *) ev;
  PE_RING_DETACH(&tm->tmring);
  EvACTIVE_off(ev);
}

static void
pe_timer_FETCH(pe_event *_ev, SV *svkey)
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
      ret = sv_2mortal(newSVnv(ev->at));
      break;
    }
    break;
  case 'h':
    if (len == 4 && memEQ(key, "hard", 4)) {
      ret = sv_2mortal(newSViv(ev->hard));
      break;
    }
    break;
  case 'w':
    if (len == 4 && memEQ(key, "when", 4)) {
      warn("Please use 'at' instead of 'when'");
      ret = sv_2mortal(newSVnv(ev->at));
      break;
    }
    break;
  case 'i':
    if (len == 8 && memEQ(key, "interval", 8)) {
      ret = sv_2mortal(newSVnv(ev->interval));
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
    if (len == 4 && memEQ(key, "hard", 4)) { ev->hard = SvIV(nval); ok=1; break; }
    break;
  case 'a':
    if (len == 2 && memEQ(key, "at", 2)) {
      ok=1;
      ev->at = SvNV(nval);
      if (EvACTIVE(ev)) {
	pe_timer_stop(_ev);
	pe_timer_start(_ev, 0);
      }
      break;
    }
    break;
  case 'i':
    if (len == 8 && memEQ(key, "interval", 8)) {
      ev->interval = SvNV(nval);
      ok=1;
      break;
    }
    break;
  }
  if (!ok) (ev->base.vtbl->up->STORE)(_ev, svkey, nval);
}

static void
boot_timer()
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
  PE_RING_INIT(&Timers, 0);
  memcpy(vt, &pe_event_base_vtbl, sizeof(pe_event_base_vtbl));
  vt->up = &pe_event_base_vtbl;
  vt->stash = (HV*) SvREFCNT_inc((SV*) gv_stashpv("Event::timer",1));
  vt->keys = sizeof(keylist)/sizeof(char*);
  vt->keylist = keylist;
  vt->FETCH = pe_timer_FETCH;
  vt->STORE = pe_timer_STORE;
  vt->start = pe_timer_start;
  vt->stop = pe_timer_stop;
  pe_register_vtbl(vt);
}
