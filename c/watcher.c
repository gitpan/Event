static int NextID = 0;
static pe_ring AllWatchers;
static struct pe_watcher_vtbl pe_watcher_base_vtbl;

static void pe_watcher_init(pe_watcher *ev)
{
  STRLEN n_a;
  assert(ev);
  assert(ev->vtbl);
  if (!ev->vtbl->stash)
    croak("sub-class VTBL must have a stash (doesn't!)");
  if (!ev->vtbl->did_require) {
    SV *tmp;
    char *name = HvNAME(ev->vtbl->stash);
    if (memEQ(name, "Event::", 7))
      name += 7;
    tmp = sv_2mortal(newSVpvf("Event/%s.pm", name));
    perl_require_pv(SvPV(tmp, n_a));
    if (sv_true(ERRSV))
      croak("Event: could not load perl support code for Event::%s: %s",
	    name, SvPV(ERRSV,n_a));
    ++ev->vtbl->did_require;
  }
  ev->stash = ev->vtbl->stash;
  ev->mysv = 0;
  PE_RING_INIT(&ev->all, ev);
  PE_RING_INIT(&ev->events, 0);
  PE_RING_UNSHIFT(&ev->all, &AllWatchers);
  EvFLAGS(ev) = 0;
  EvINVOKE1_on(ev);
  EvREENTRANT_on(ev);
  EvCLUMP_on(ev);
  ev->FALLBACK = 0;
  NextID = (NextID+1) & 0x7fff; /* make it look like the kernel :-, */
  ev->id = NextID;
  ev->desc = newSVpvn("??",2);
  ev->running = 0;
  ev->cbtime = 0;
  ev->priority = PE_QUEUES;
  ev->callback = 0;
  ev->ext_data = 0;
  ev->stats = 0;
}

static void pe_watcher_cancel_events(pe_watcher *wa)
{
  pe_event *ev;
  while (!PE_RING_EMPTY(&wa->events)) {
    PE_RING_POP(&wa->events, ev);
    (*ev->vtbl->dtor)(ev);
  }
}

static void pe_watcher_dtor(pe_watcher *ev)
{
  STRLEN n_a;
  int xx;
  assert(EvCANCELLED(ev));
  if (EvDESTROYED(ev)) {
    warn("Attempt to destroy watcher %x again", ev);
    return;
  }
  if (EvDEBUGx(ev) >= 3)
      warn("Watcher '%s' destroyed", SvPV(ev->desc, n_a));
  EvDESTROYED_on(ev);
  assert(PE_RING_EMPTY(&ev->events));
  PE_RING_DETACH(&ev->all);
  if (ev->mysv) {
      invalidate_sv(ev->mysv);
      ev->mysv=0;
  }
  if (ev->FALLBACK)
    SvREFCNT_dec(ev->FALLBACK);
  if (ev->desc)
    SvREFCNT_dec(ev->desc);
  if (EvPERLCB(ev))
    SvREFCNT_dec(ev->callback);
  if (ev->stats)
    Estat.dtor(ev->stats);
  safefree(ev);
}

/********************************** *******************************/

WKEYMETH(_watcher_callback)
{
  if (!nval) {
    SV *ret = (EvPERLCB(ev)? (SV*) ev->callback :
	       sv_2mortal(newSVpvf("<FPTR=0x%x EXT=0x%x>",
				   ev->callback, ev->ext_data)));
    dSP;
    XPUSHs(ret);
    PUTBACK;
  } else {
    SV *sv;
    SV *old=0;
    if (EvPERLCB(ev))
      old = (SV*) ev->callback;
    EvPERLCB_on(ev);
    if (!SvROK(nval) ||
	(SvTYPE(sv=SvRV(nval)) != SVt_PVCV &&
	 (SvTYPE(sv) != SVt_PVAV || av_len((AV*)sv) != 1))) {
      sv_dump(sv);
      croak("Callback must be a code ref or two element array ref");
    }
    ev->callback = SvREFCNT_inc(nval);
    if (old)
      SvREFCNT_dec(old);
  }
}

WKEYMETH(_watcher_clump)
{
  if (!nval) {
    dSP;
    XPUSHs(boolSV(EvCLUMP(ev)));
    PUTBACK;
  } else {
    if (sv_true(nval)) EvCLUMP_on(ev); else EvCLUMP_off(ev);
  }
}

WKEYMETH(_watcher_desc)
{
  if (!nval) {
    dSP;
    XPUSHs(ev->desc);
    PUTBACK;
  } else {
    sv_setsv(ev->desc, nval);
  }
}

WKEYMETH(_watcher_debug)
{
  if (!nval) {
    dSP;
    XPUSHs(boolSV(EvDEBUG(ev)));
    PUTBACK;
  } else {
    if (sv_true(nval)) EvDEBUG_on(ev); else EvDEBUG_off(ev);
  }
}

WKEYMETH(_watcher_flags)
{
  if (!nval) {
    dSP;
    XPUSHs(sv_2mortal(newSViv((ev->flags & PE_VISIBLE_FLAGS) |
			      (ev->running? PE_RUNNING : 0) |
			      (PE_RING_EMPTY(&ev->events)? 0 : PE_QUEUED))));
    PUTBACK;
  } else
    croak("'e_flags' are read-only");
}

WKEYMETH(_watcher_id)
{
  if (!nval) {
    dSP;
    XPUSHs(sv_2mortal(newSViv(ev->id)));
    PUTBACK;
  } else
    croak("'e_id' is read-only");
}

WKEYMETH(_watcher_priority)
{
  if (!nval) {
    dSP;
    XPUSHs(sv_2mortal(newSViv(ev->priority)));
    PUTBACK;
  } else
    ev->priority = SvIV(nval);
}

WKEYMETH(_watcher_reentrant)
{
  if (!nval) {
    dSP;
    XPUSHs(boolSV(EvREENTRANT(ev)));
    PUTBACK;
  } else {
    if (sv_true(nval))
      EvREENTRANT_on(ev);
    else {
      if (ev->running > 1)
	croak("'reentrant' cannot be turned off while nested %d times",
	      ev->running);
      EvREENTRANT_off(ev);
    }
  }
}

WKEYMETH(_watcher_repeat)
{
  if (!nval) {
    dSP;
    XPUSHs(boolSV(EvREPEAT(ev)));
    PUTBACK;
  } else {
    if (sv_true(nval)) EvREPEAT_on(ev); else EvREPEAT_off(ev);
  }
}

WKEYMETH(_watcher_running)
{
  if (!nval) {
    dSP;
    XPUSHs(sv_2mortal(newSViv(ev->running)));
    PUTBACK;
  } else
    croak("'e_running' is read-only");
}

/********************************** *******************************/

static void pe_watcher_FETCH(void *vptr, SV *svkey)
{
  pe_watcher *ev = (pe_watcher *) vptr;
  SV **mp;
  STRLEN len;
  char *key = SvPV(svkey, len);
  if (!len) return;
  mp = hv_fetch(ev->vtbl->keymethod, key, len, 0);
  if (mp) {
    assert(*mp && SvIOK(*mp));
    ((void(*)(pe_watcher*,SV*)) SvIVX(*mp))(ev,0);
  }
  else if (ev->FALLBACK) {
    SV **svp = hv_fetch(ev->FALLBACK, key, len, 0);
    if (svp) {
      dSP;
      XPUSHs(*svp);
      PUTBACK;
    }
  }
}

static void pe_watcher_STORE(void *vptr, SV *svkey, SV *nval)
{
  pe_watcher *ev = (pe_watcher *) vptr;
  SV **mp;
  STRLEN len;
  char *key = SvPV(svkey, len);
  if (!len) return;
  mp = hv_fetch(ev->vtbl->keymethod, key, len, 0);
  if (mp) {
    assert(*mp && SvIOK(*mp));
    ((void(*)(pe_watcher*,SV*)) SvIVX(*mp))(ev,nval);
  }
  else
    pe_watcher_STORE_FALLBACK(ev, svkey, nval);
}

static void pe_watcher_FIRSTKEY(void *vptr);

static void pe_watcher_NEXTKEY(void *vptr)
{
  pe_watcher *ev = (pe_watcher *) vptr;
  HE *he=0;
  switch (ev->iter) {
  case PE_ITER_WATCHER:
    he = hv_iternext(ev->vtbl->keymethod);
    if (he)
      break;
    else
      ev->iter = PE_ITER_FALLBACK;
  case PE_ITER_FALLBACK:
    if (ev->FALLBACK)
      he = hv_iternext(ev->FALLBACK);
    break;
  default:
    pe_watcher_FIRSTKEY(vptr);
    return;
  }
  if (he) {
    dSP;
    XPUSHs(hv_iterkeysv(he));
    PUTBACK;
  }
}

static void pe_watcher_FIRSTKEY(void *vptr)
{
  pe_watcher *ev = (pe_watcher *) vptr;
  ev->iter = PE_ITER_WATCHER;
  hv_iterinit(ev->vtbl->keymethod);
  if (ev->FALLBACK)
    hv_iterinit(ev->FALLBACK);
  pe_watcher_NEXTKEY(ev);
}

static void pe_watcher_DELETE(void *vptr, SV *svkey)
{
  STRLEN n_a;
  pe_watcher *ev = (pe_watcher *) vptr;
  SV *ret;
  if (hv_exists_ent(ev->vtbl->keymethod, svkey, 0))
    croak("Cannot delete key '%s'", SvPV(svkey,n_a));
  if (!ev->FALLBACK)
    return;
  ret = hv_delete_ent(ev->FALLBACK, svkey, 0, 0);
  if (ret && GIMME_V != G_VOID) {
    dSP;
    XPUSHs(sv_2mortal(ret));
    PUTBACK;
  }
}

static int pe_watcher_EXISTS(void *vptr, SV *svkey)
{
  pe_watcher *ev = (pe_watcher *) vptr;
  if (hv_exists_ent(ev->vtbl->keymethod, svkey, 0))
    return 1;
  if (!ev->FALLBACK)
    return 0;
  return hv_exists_ent(ev->FALLBACK, svkey, 0);
}

/********************************** *******************************/

static void pe_watcher_nomethod(pe_watcher *ev, char *meth)
{
  HV *stash = ev->vtbl->stash;
  assert(stash);
  croak("%s::%s is missing", HvNAME(stash), meth);
}

static void pe_watcher_nostart(pe_watcher *ev, int repeat)
{ pe_watcher_nomethod(ev,"start"); }
static void pe_watcher_nostop(pe_watcher *ev)
{ pe_watcher_nomethod(ev,"stop"); }
static void pe_watcher_alarm(pe_watcher *ev, pe_timeable *tm)
{ pe_watcher_nomethod(ev,"alarm"); }

static void boot_pe_watcher()
{
  HV *stash = gv_stashpv("Event::Watcher", 1);
  struct pe_watcher_vtbl *vt;
  PE_RING_INIT(&AllWatchers, 0);
  vt = &pe_watcher_base_vtbl;
  vt->base.Fetch = pe_watcher_FETCH;
  vt->base.Store = pe_watcher_STORE;
  vt->base.Firstkey = pe_watcher_FIRSTKEY;
  vt->base.Nextkey = pe_watcher_NEXTKEY;
  vt->base.Delete = pe_watcher_DELETE;
  vt->base.Exists = pe_watcher_EXISTS;
  vt->stash = 0;
  vt->did_require = 0;
  vt->keymethod = newHV();
  hv_store(vt->keymethod, "e_cb",         4, newSViv((IV)_watcher_callback), 0);
  hv_store(vt->keymethod, "e_clump",      7, newSViv((IV)_watcher_clump), 0);
  hv_store(vt->keymethod, "e_desc",       6, newSViv((IV)_watcher_desc), 0);
  hv_store(vt->keymethod, "e_debug",      7, newSViv((IV)_watcher_debug), 0);
  hv_store(vt->keymethod, "e_flags",      7, newSViv((IV)_watcher_flags), 0);
  hv_store(vt->keymethod, "e_id",         4, newSViv((IV)_watcher_id), 0);
  hv_store(vt->keymethod, "e_prio",       6, newSViv((IV)_watcher_priority), 0);
  hv_store(vt->keymethod, "e_reentrant", 11, newSViv((IV)_watcher_reentrant), 0);
  hv_store(vt->keymethod, "e_repeat",     8, newSViv((IV)_watcher_repeat), 0);
  hv_store(vt->keymethod, "e_running",    9, newSViv((IV)_watcher_running), 0);
  vt->dtor = pe_watcher_dtor;
  vt->start = pe_watcher_nostart;
  vt->stop = pe_watcher_nostop;
  vt->alarm = pe_watcher_alarm;
  newCONSTSUB(stash, "ACTIVE", newSViv(PE_ACTIVE));
  newCONSTSUB(stash, "SUSPEND", newSViv(PE_SUSPEND));
  newCONSTSUB(stash, "QUEUED", newSViv(PE_QUEUED));
  newCONSTSUB(stash, "RUNNING", newSViv(PE_RUNNING));
  newCONSTSUB(stash, "R", newSViv(PE_R));
  newCONSTSUB(stash, "W", newSViv(PE_W));
  newCONSTSUB(stash, "E", newSViv(PE_E));
  newCONSTSUB(stash, "T", newSViv(PE_T));
}

static void pe_register_vtbl(pe_watcher_vtbl *vt, HV *stash,
			     pe_event_vtbl *evt)
{
  vt->stash = stash;
  SvREFCNT_inc(stash);
  vt->event_vtbl = evt;
  vt->new_event = evt->new_event;
}

static void pe_watcher_now(pe_watcher *wa)
{
  pe_event *ev;
  if (EvSUSPEND(wa)) return;
  EvRUNNOW_on(wa);
  ev = (*wa->vtbl->new_event)(wa);
  ++ev->count;
  queueEvent(ev);
}

/******************************************
  The following methods change the status flags.  These are the only
  methods that should venture to change these flags!
 */

static void pe_watcher_cancel(pe_watcher *ev)
{
  EvSUSPEND_off(ev);
  pe_watcher_stop(ev, 1);
  EvCANCELLED_on(ev);
  if (EvCANDESTROY(ev))
    (*ev->vtbl->dtor)(ev);
}

static void pe_watcher_suspend(pe_watcher *ev)
{
  STRLEN n_a;
  if (EvSUSPEND(ev))
    return;
  if (EvDEBUGx(ev) >= 4)
    warn("Event: suspend '%s'\n", SvPV(ev->desc,n_a));
  pe_watcher_off(ev);
  pe_watcher_cancel_events(ev);
  EvSUSPEND_on(ev); /* must happen nowhere else!! */
}

static void pe_watcher_resume(pe_watcher *ev)
{
  STRLEN n_a;
  if (!EvSUSPEND(ev))
    return;
  EvSUSPEND_off(ev);
  if (EvDEBUGx(ev) >= 4)
    warn("Event: resume '%s'%s%s\n", SvPV(ev->desc,n_a),
	 EvACTIVE(ev)?" ACTIVE":"");
  if (EvACTIVE(ev))
    pe_watcher_on(ev, 0);
}

static void pe_watcher_on(pe_watcher *wa, int repeat)
{
  if (EvPOLLING(wa) || EvSUSPEND(wa)) return;
  assert(EvACTIVE(wa));
  (*wa->vtbl->start)(wa, repeat);
  EvPOLLING_on(wa); /* must happen nowhere else!! */
}

static void pe_watcher_off(pe_watcher *wa)
{
  if (!EvPOLLING(wa) || EvSUSPEND(wa)) return;
  (*wa->vtbl->stop)(wa);
  EvPOLLING_off(wa);
}

static void pe_watcher_start(pe_watcher *ev, int repeat)
{
  STRLEN n_a;
  if (EvACTIVE(ev))
    return;
  if (EvDEBUGx(ev) >= 4)
    warn("Event: active ON '%s'\n", SvPV(ev->desc,n_a));
  EvACTIVE_on(ev); /* must happen nowhere else!! */
  pe_watcher_on(ev, repeat);
  ++ActiveWatchers;
}

static void pe_watcher_stop(pe_watcher *ev, int cancel_events)
{
  STRLEN n_a;
  if (!EvACTIVE(ev))
    return;
  if (EvDEBUGx(ev) >= 4)
    warn("Event: active OFF '%s'\n", SvPV(ev->desc,n_a));
  EvACTIVE_off(ev); /* must happen nowhere else!! */
  pe_watcher_off(ev);
  if (cancel_events) pe_watcher_cancel_events(ev);
  --ActiveWatchers;
}

