static struct pe_watcher_vtbl pe_tied_vtbl;

static pe_watcher *pe_tied_allocate(SV *class)
{
  pe_tied *ev;
  New(PE_NEWID, ev, 1, pe_tied);
  ev->base.vtbl = &pe_tied_vtbl;
  pe_watcher_init((pe_watcher*)ev);
  PE_RING_INIT(&ev->tm.ring, ev);
  ev->base.stash = gv_stashsv(class, 1);
  return (pe_watcher*) ev;
}

static void pe_tied_start(pe_watcher *ev, int repeat)
{
  GV *gv;
  dSP;
  PUSHMARK(SP);
  XPUSHs(watcher_2sv(ev));
  XPUSHs(boolSV(repeat));
  PUTBACK;
  gv = gv_fetchmethod(ev->stash, "_start");
  if (!gv)
    croak("Cannot find %s->_start()", HvNAME(ev->stash));
  perl_call_sv((SV*)GvCV(gv), G_DISCARD);
}

static void pe_tied_stop(pe_watcher *ev)
{
  GV *gv = gv_fetchmethod(ev->stash, "_stop");
  pe_timeable_stop(&((pe_tied*)ev)->tm);
  if (gv) {
    dSP;
    PUSHMARK(SP);
    XPUSHs(watcher_2sv(ev));
    PUTBACK;
    perl_call_sv((SV*)GvCV(gv), G_DISCARD);
  }
}

static void pe_tied_alarm(pe_watcher *ev, pe_timeable *_ign)
{
  GV *gv;
  dSP;
  PUSHMARK(SP);
  XPUSHs(watcher_2sv(ev));
  PUTBACK;
  gv = gv_fetchmethod(ev->stash, "_alarm");
  if (!gv)
    croak("Cannot find %s->_alarm()", HvNAME(ev->stash));
  perl_call_sv((SV*)GvCV(gv), G_DISCARD);
}

WKEYMETH(_tied_at)
{
  pe_tied *tp = (pe_tied*) ev;
  if (!nval) {
    dSP;
    XPUSHs(sv_2mortal(newSVnv(tp->tm.at)));
    PUTBACK;
  } else {
    pe_timeable_stop(&tp->tm);
    if (SvOK(nval)) {
      tp->tm.at = SvNV(nval);
      pe_timeable_start(&tp->tm);
    }
  }
}

WKEYMETH(_tied_cbtime)
{
  if (!nval) {
    dSP;
    XPUSHs(sv_2mortal(newSVnv(ev->cbtime)));
    PUTBACK;
  } else
    croak("'e_cbtime' is read-only");
}

WKEYMETH(_tied_flags)
{
  if (!nval) {
    _watcher_flags(ev, nval);
  } else {
    IV nflags = SvIV(nval);
    IV flip = nflags ^ ev->flags;
    IV other = flip & ~(PE_CBTIME|PE_INVOKE1);
    if (flip & PE_INVOKE1) {
      if (nflags & PE_INVOKE1) EvINVOKE1_on(ev); else EvINVOKE1_off(ev);
    }
    if (flip & PE_CBTIME) {
      if (nflags & PE_CBTIME) EvCBTIME_on(ev); else EvCBTIME_off(ev);
    }
    if (other)
      warn("Other flags (0x%x) cannot be changed", other);
  }
}

static void boot_tied()
{
  pe_watcher_vtbl *vt = &pe_tied_vtbl;
  memcpy(vt, &pe_watcher_base_vtbl, sizeof(pe_watcher_base_vtbl));
  vt->did_require = 1; /* otherwise tries to autoload Event::Event! */
  vt->keymethod = newHVhv(vt->keymethod);
  hv_store(vt->keymethod, "e_at", 4, newSViv((IV)_tied_at), 0);
  hv_store(vt->keymethod, "e_cbtime", 8, newSViv((IV)_tied_cbtime), 0);
  hv_store(vt->keymethod, "e_flags", 7, newSViv((IV)_tied_flags), 0);
  hv_store(vt->keymethod, "e_hard", 6, newSViv((IV)_timeable_hard), 0);
  vt->start = pe_tied_start;
  vt->stop = pe_tied_stop;
  vt->alarm = pe_tied_alarm;
  pe_register_vtbl(vt, gv_stashpv("Event",1), &event_vtbl);
}
