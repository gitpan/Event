static struct pe_event_vtbl pe_watchvar_vtbl;

typedef struct pe_watchvar pe_watchvar;
struct pe_watchvar {
  pe_event base;
  SV *variable;
};

static pe_event *
pe_watchvar_allocate()
{
  pe_watchvar *ev;
  New(PE_NEWID, ev, 1, pe_watchvar);
  ev->base.vtbl = &pe_watchvar_vtbl;
  pe_event_init((pe_event*) ev);
  ev->variable = 0;
  EvREPEAT_on(ev);
  return (pe_event*) ev;
}

static void
pe_watchvar_dtor(pe_event *ev)
{
  pe_watchvar *wv = (pe_watchvar *)ev;
  if (wv->variable)
    SvREFCNT_dec(wv->variable);
  (*ev->vtbl->up->dtor)(ev);
}

static I32
tracevar(ix, sv)
IV ix;
SV *sv;
{
/*    dXSARGS;*/
    pe_event *ev = (pe_event *)ix;
    /* Taken From tkGlue.c

       We are a "magic" set processor.
       So we are (I think) supposed to look at "private" flags 
       and set the public ones if appropriate.
       e.g. "chop" sets SvPOKp as a hint but not SvPOK

       presumably other operators set other private bits.

       Question are successive "magics" called in correct order?

       i.e. if we are tracing a tied variable should we call 
       some magic list or be careful how we insert ourselves in the list?
    */

    if (!SvPOK(sv) && SvPOKp(sv))
	SvPOK_on(sv);

    if (!SvNOK(sv) && SvNOKp(sv))
	SvNOK_on(sv);

    if (!SvIOK(sv) && SvIOKp(sv))
	SvIOK_on(sv);

    queueEvent(ev, 1);

    return 0; /*ignored*/
}

static void
pe_watchvar_start(pe_event *_ev, int repeat)
{
    dTHR;
    struct ufuncs *ufp;
    MAGIC **mgp;
    MAGIC *mg;
    pe_watchvar *ev = (pe_watchvar*) _ev;
    SV *sv = ev->variable;

    EvSUSPEND_off(ev);
    if (EvACTIVE(ev))
      return;
    if (!sv)
      croak("No variable specified");
    sv = SvRV(sv);
    if (SvREADONLY(sv))
      croak("Cannot trace readonly variable");
    if (!SvUPGRADE(sv, SVt_PVMG))
      croak("Trace SvUPGRADE failed");
    if (mg_find(sv, 'U'))
      croak("Variable already being traced");

    mgp = &SvMAGIC(sv);
    while ((mg = *mgp)) {
	mgp = &mg->mg_moremagic;
    }

    Newz(PE_NEWID, mg, 1, MAGIC);
    mg->mg_type = 'U';
    mg->mg_virtual = &vtbl_uvar;
    *mgp = mg;

    New(PE_NEWID, ufp, 1, struct ufuncs);
    ufp->uf_val = 0;
    ufp->uf_set = tracevar;
    ufp->uf_index = (IV) ev;
    mg->mg_ptr = (char *) ufp;

    mg_magical(sv);
    if (!SvMAGICAL(sv))
      croak("mg_magical didn't");
    EvACTIVE_on(ev);
}

static void
pe_watchvar_stop(pe_event *_ev)
{
    MAGIC **mgp;
    MAGIC *mg;
    MAGIC *mgtmp;
    pe_watchvar *ev = (pe_watchvar*) _ev;
    SV *sv = SvRV(ev->variable);

    if (!EvACTIVE(ev))
      return;
    EvACTIVE_off(ev);

    if (SvTYPE(sv) < SVt_PVMG || !SvMAGIC(sv))
        return;

    mgp = &SvMAGIC(sv);
    while ((mg = *mgp)) {
	if (mg->mg_obj == (SV*)ev)
	    break;
	mgp = &mg->mg_moremagic;
    }

    if(!mg)
	return;

    *mgp = mg->mg_moremagic;

    safefree(mg->mg_ptr);
    safefree(mg);
}

static void
pe_watchvar_FETCH(pe_event *_ev, SV *svkey)
{
  pe_watchvar *ev = (pe_watchvar*) _ev;
  SV *ret=0;
  STRLEN len;
  char *key = SvPV(svkey, len);
  if (len && key[0] == '-') { ++key; --len; }
  if (!len) return;
  switch (key[0]) {
  case 'v':
    if (len == 8 && memEQ(key, "variable", 8)) {
      ret = ev->variable;
      break;
    }
    break;
  }
  if (ret) {
    dSP;
    XPUSHs(ret);
    PUTBACK;
  } else {
    (*_ev->vtbl->up->FETCH)(_ev, svkey);
  }
}

static void
pe_watchvar_STORE(pe_event *_ev, SV *svkey, SV *nval)
{
  pe_watchvar *ev = (pe_watchvar *)_ev;
  STRLEN len;
  char *key = SvPV(svkey, len);
  int ok=0;
  if (len && key[0] == '-') { ++key; --len; }
  if (!len) return;
  switch (key[0]) {
  case 'v':
    if (len == 8 && memEQ(key, "variable", 8)) {
      int active = EvACTIVE(ev);
      if (!SvROK(nval))
	croak("Expecting a reference");
      ok=1;
      if (active)
	(*_ev->vtbl->stop)(_ev);
      if (ev->variable)
	SvREFCNT_dec(ev->variable);
      ev->variable = SvREFCNT_inc(nval);
      if (active)
	(*_ev->vtbl->start)(_ev, 0);
      break;
    }
    break;
  }
  if (!ok) (_ev->vtbl->up->STORE)(_ev, svkey, nval);
}

static void
boot_watchvar()
{
  static char *keylist[] = {
    "variable"
  };
  pe_event_vtbl *vt = &pe_watchvar_vtbl;
  memcpy(vt, &pe_event_base_vtbl, sizeof(pe_event_base_vtbl));
  vt->up = &pe_event_base_vtbl;
  vt->keys = sizeof(keylist)/sizeof(char*);
  vt->keylist = keylist;
  vt->dtor = pe_watchvar_dtor;
  vt->stash = (HV*) SvREFCNT_inc((SV*) gv_stashpv("Event::watchvar",1));
  vt->FETCH = pe_watchvar_FETCH;
  vt->STORE = pe_watchvar_STORE;
  vt->invoke = pe_event_invoke_repeat;
  vt->start = pe_watchvar_start;
  vt->stop = pe_watchvar_stop;
  pe_register_vtbl(vt);
}

