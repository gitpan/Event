static struct pe_event_vtbl pe_tied_vtbl;

static pe_event *
pe_tied_allocate(SV *class)
{
  pe_event *ev;
  New(PE_NEWID, ev, 1, pe_event);
  ev->vtbl = &pe_tied_vtbl;
  pe_event_init(ev);
  ev->stash = gv_stashsv(class, 1);
  return ev;
}

static void
pe_tied_start(pe_event *ev, int repeat)
{
  GV *gv;
  dSP;
  PUSHMARK(SP);
  XPUSHs(sv_2mortal(event_2sv(ev)));
  XPUSHs(boolSV(repeat));
  PUTBACK;
  gv = gv_fetchmethod(ev->stash, "start");
  if (!gv)
    croak("Cannot find %s->start()", HvNAME(ev->stash));
  perl_call_sv((SV*)GvCV(gv), G_DISCARD);
}

static void
pe_tied_stop(pe_event *ev)
{
  GV *gv;
  dSP;
  PUSHMARK(SP);
  XPUSHs(sv_2mortal(event_2sv(ev)));
  PUTBACK;
  gv = gv_fetchmethod(ev->stash, "stop");
  if (!gv)
    croak("Cannot find %s->stop()", HvNAME(ev->stash));
  perl_call_sv((SV*)GvCV(gv), G_DISCARD);
}

static void
boot_tied()
{
  pe_event_vtbl *vt = &pe_tied_vtbl;
  memcpy(vt, &pe_event_base_vtbl, sizeof(pe_event_base_vtbl));
  vt->up = &pe_event_base_vtbl;
  vt->stash = (HV*) SvREFCNT_inc((SV*) gv_stashpv("Event",1));
  vt->keys = 0;
  vt->start = pe_tied_start;
  vt->stop = pe_tied_stop;
  pe_register_vtbl(vt);
}
