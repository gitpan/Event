static struct pe_event_vtbl pe_tied_vtbl;

static pe_event *pe_tied_allocate(SV *class)
{
  pe_tmevent *ev;
  New(PE_NEWID, ev, 1, pe_tmevent);
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
  GV *gv;
  dSP;
  PUSHMARK(SP);
  XPUSHs(sv_2mortal(event_2sv(ev)));
  PUTBACK;
  gv = gv_fetchmethod(ev->stash, "_stop");
  if (!gv)
    croak("Cannot find %s->_stop()", HvNAME(ev->stash));
  perl_call_sv((SV*)GvCV(gv), G_DISCARD);
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
  case 'f':
    if (len == 5 && memEQ(key, "flags", 5)) {
      IV flip = SvIV(nval) ^ ev->flags;
      if (flip & PE_INVOKE1) {
	if (EvINVOKE1(SvIV(nval))) EvINVOKE1_on(ev); else EvINVOKE1_off(ev);
      }
      else
	croak("'flags' are mostly read-only");
      ok=1;
    }
    break;
  }
  if (!ok) (ev->vtbl->up->STORE)(ev, svkey, nval);
}

static void boot_tied()
{
  pe_event_vtbl *vt = &pe_tied_vtbl;
  memcpy(vt, &pe_event_base_vtbl, sizeof(pe_event_base_vtbl));
  vt->up = &pe_event_base_vtbl;
  vt->stash = (HV*) SvREFCNT_inc((SV*) gv_stashpv("Event",1));
  vt->keys = 0;
  vt->STORE = pe_tied_STORE;
  vt->start = pe_tied_start;
  vt->stop = pe_tied_stop;
  pe_register_vtbl(vt);
}
