static struct pe_event_vtbl pe_tied_vtbl;

static pe_event *pe_tied_allocate(SV *class)
{
  pe_tied *ev;
  New(PE_NEWID, ev, 1, pe_tied);
  ev->base.vtbl = &pe_tied_vtbl;
  pe_event_init((pe_event*)ev);
  PE_RING_INIT(&ev->tm.ring, ev);
  ev->base.stash = gv_stashsv(class, 1);
  return (pe_event*) ev;
}

static void pe_tied_start(pe_event *ev, int repeat)
{
  GV *gv;
  dSP;
  PUSHMARK(SP);
  XPUSHs(sv_2mortal(event_2sv(ev)));
  XPUSHs(boolSV(repeat));
  PUTBACK;
  gv = gv_fetchmethod(ev->stash, "_start");
  if (!gv)
    croak("Cannot find %s->_start()", HvNAME(ev->stash));
  perl_call_sv((SV*)GvCV(gv), G_DISCARD);
}

static void pe_tied_stop(pe_event *ev)
{
  GV *gv = gv_fetchmethod(ev->stash, "_stop");
  pe_timeable_stop(&((pe_tied*)ev)->tm);
  if (gv) {
    dSP;
    PUSHMARK(SP);
    XPUSHs(sv_2mortal(event_2sv(ev)));
    PUTBACK;
    perl_call_sv((SV*)GvCV(gv), G_DISCARD);
  }
}

static void pe_tied_alarm(pe_event *ev, pe_timeable *_ign)
{
  GV *gv;
  dSP;
  PUSHMARK(SP);
  XPUSHs(sv_2mortal(event_2sv(ev)));
  PUTBACK;
  gv = gv_fetchmethod(ev->stash, "_alarm");
  if (!gv)
    croak("Cannot find %s->_alarm()", HvNAME(ev->stash));
  perl_call_sv((SV*)GvCV(gv), G_DISCARD);
}

static void pe_tied_postCB(pe_cbframe *fp)
{
  pe_event *ev = fp->ev;
  GV *gv = gv_fetchmethod(ev->stash, "_postCB");
  if (gv) {
    dSP;
    PUSHMARK(SP);
    XPUSHs(sv_2mortal(event_2sv(ev)));
    PUTBACK;
    perl_call_sv((SV*)GvCV(gv), G_DISCARD);
  }
  pe_event_postCB(fp);
}

static void pe_tied_FETCH(pe_event *_ev, SV *svkey)
{
  pe_tied *ev = (pe_tied*) _ev;
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
  case 'c':
    if (len == 6 && memEQ(key, "cbtime", 6)) {
      ret = sv_2mortal(newSVnv(ev->base.cbtime));
      break;
    }
  }
  if (ret) {
    dSP;
    XPUSHs(ret);
    PUTBACK;
  } else {
    (*ev->base.vtbl->up->FETCH)(_ev, svkey);
  }
}

static void pe_tied_STORE(pe_event *ev, SV *svkey, SV *nval)
{
  SV *old=0;
  STRLEN len;
  char *key = SvPV(svkey, len);
  int ok=0;
  if (len && key[0] == '-') { ++key; --len; }
  if (!len) return;
  switch (key[0]) {
  case 'a':
    if (len == 2 && memEQ(key, "at", 2)) {
      pe_timeable_stop(&((pe_tied*)ev)->tm);
      if (SvOK(nval)) {
	((pe_tied*)ev)->tm.at = SvNV(nval);
	pe_timeable_start(&((pe_tied*)ev)->tm);
      }
      break;
    }
    break;
  case 'c':
    if (len == 6 && memEQ(key, "cbtime", 6))
      croak("'cbtime' is read-only");
    break;
  case 'f':
    if (len == 5 && memEQ(key, "flags", 5)) {
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
      ok=1;
    }
    break;
  }
  if (!ok) (ev->vtbl->up->STORE)(ev, svkey, nval);
}

static void boot_tied()
{
  static char *keylist[] = {
    "at",
    "cbtime"
  };
  pe_event_vtbl *vt = &pe_tied_vtbl;
  memcpy(vt, &pe_event_base_vtbl, sizeof(pe_event_base_vtbl));
  vt->up = &pe_event_base_vtbl;
  vt->stash = (HV*) SvREFCNT_inc((SV*) gv_stashpv("Event",1));
  vt->keys = sizeof(keylist)/sizeof(char*);
  vt->keylist = keylist;
  vt->STORE = pe_tied_STORE;
  vt->start = pe_tied_start;
  vt->stop = pe_tied_stop;
  vt->alarm = pe_tied_alarm;
  vt->postCB = pe_tied_postCB;
  pe_register_vtbl(vt);
}
