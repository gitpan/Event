static struct pe_event_vtbl pe_var_vtbl;

static pe_event *pe_var_allocate()
{
  pe_var *ev;
  New(PE_NEWID, ev, 1, pe_var);
  ev->base.vtbl = &pe_var_vtbl;
  pe_event_init((pe_event*) ev);
  ev->variable = &PL_sv_undef;
  ev->events = PE_W;
  ev->got = 0;
  EvREPEAT_on(ev);
  EvINVOKE1_off(ev);
  return (pe_event*) ev;
}

static void pe_var_dtor(pe_event *ev)
{
  pe_var *wv = (pe_var *)ev;
  SvREFCNT_dec(wv->variable);
  (*ev->vtbl->up->dtor)(ev);
}

static void pe_tracevar(pe_event *ev, SV *sv)
{
    /* Adapted from tkGlue.c

       We are a "magic" set processor.
       So we are (I think) supposed to look at "private" flags 
       and set the public ones if appropriate.
       e.g. "chop" sets SvPOKp as a hint but not SvPOK

       presumably other operators set other private bits.

       Question are successive "magics" called in correct order?

       i.e. if we are tracing a tied variable should we call 
       some magic list or be careful how we insert ourselves in the list?
    */

    if (SvPOKp(sv)) SvPOK_on(sv);
    if (SvNOKp(sv)) SvNOK_on(sv);
    if (SvIOKp(sv)) SvIOK_on(sv);

    queueEvent(ev, 1);
}

static I32 tracevar_r(IV ix, SV *sv)
{
    pe_event *ev = (pe_event *)ix;
    ((pe_var*)ev)->got |= PE_R;
    pe_tracevar(ev, sv);
    return 0; /*ignored*/
}
static I32 tracevar_w(IV ix, SV *sv)
{
    pe_event *ev = (pe_event *)ix;
    ((pe_var*)ev)->got |= PE_W;
    pe_tracevar(ev, sv);
    return 0; /*ignored*/
}

static void pe_var_start(pe_event *_ev, int repeat)
{
    dTHR;
    struct ufuncs *ufp;
    MAGIC **mgp;
    MAGIC *mg;
    pe_var *ev = (pe_var*) _ev;
    SV *sv = ev->variable;

    if (!sv || !SvOK(sv))
      croak("No variable specified");
    if (!ev->events)
      croak("No events specified");
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
    ufp->uf_val = ev->events & PE_R? tracevar_r : 0;
    ufp->uf_set = ev->events & PE_W? tracevar_w : 0;
    ufp->uf_index = (IV) ev;
    mg->mg_ptr = (char *) ufp;
    mg->mg_obj = (SV*) ev; /* hope not REFCNT_dec! */

    mg_magical(sv);
    if (!SvMAGICAL(sv))
      croak("mg_magical didn't");
}

static void pe_var_stop(pe_event *_ev)
{
    MAGIC **mgp;
    MAGIC *mg;
    MAGIC *mgtmp;
    pe_var *ev = (pe_var*) _ev;
    SV *sv = SvRV(ev->variable);

    if (SvTYPE(sv) < SVt_PVMG || !SvMAGIC(sv)) {
      warn("Var unmagic'd already?");
      return;
    }

    mgp = &SvMAGIC(sv);
    while ((mg = *mgp)) {
	if (mg->mg_obj == (SV*)ev)
	    break;
	mgp = &mg->mg_moremagic;
    }

    if(!mg) {
      warn("Couldn't find var magic");
      return;
    }

    *mgp = mg->mg_moremagic;

    safefree(mg->mg_ptr);
    safefree(mg);
}

static void pe_var_postCB(pe_cbframe *fp)
{
  ((pe_var*) fp->ev)->got = 0;
  pe_event_postCB(fp);
}

static void pe_var_FETCH(pe_event *_ev, SV *svkey)
{
  pe_var *ev = (pe_var*) _ev;
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

static void pe_var_STORE(pe_event *_ev, SV *svkey, SV *nval)
{
  pe_var *ev = (pe_var *)_ev;
  STRLEN len;
  char *key = SvPV(svkey, len);
  int ok=0;
  if (len && key[0] == '-') { ++key; --len; }
  if (!len) return;
  switch (key[0]) {
  case 'e':
    if (len == 6 && memEQ(key, "events", 6)) {
      ok=1;
      ev->events = sv_2events_mask(nval, PE_R|PE_W);
      if (EvACTIVE(ev)) {
	pe_event_stop(_ev);
	pe_event_start(_ev, 0);
      }
      break;
    }
    break;
  case 'g':
    if (len == 3 && memEQ(key, "got", 3))
      croak("'got' is read-only");
    break;
  case 'v':
    if (len == 8 && memEQ(key, "variable", 8)) {
      SV *old = ev->variable;
      int active = EvACTIVE(ev);
      if (!SvROK(nval))
	croak("Expecting a reference");
      ok=1;
      if (active)	pe_event_stop(_ev);
      ev->variable = SvREFCNT_inc(nval);
      SvREFCNT_dec(old);
      if (active)	pe_event_start(_ev, 0);
      break;
    }
    break;
  }
  if (!ok) (_ev->vtbl->up->STORE)(_ev, svkey, nval);
}

static void boot_var()
{
  static char *keylist[] = {
    "variable",
    "events",
    "got"
  };
  pe_event_vtbl *vt = &pe_var_vtbl;
  memcpy(vt, &pe_event_base_vtbl, sizeof(pe_event_base_vtbl));
  vt->up = &pe_event_base_vtbl;
  vt->keys = sizeof(keylist)/sizeof(char*);
  vt->keylist = keylist;
  vt->dtor = pe_var_dtor;
  vt->stash = (HV*) SvREFCNT_inc((SV*) gv_stashpv("Event::var",1));
  vt->FETCH = pe_var_FETCH;
  vt->STORE = pe_var_STORE;
  vt->start = pe_var_start;
  vt->stop = pe_var_stop;
  vt->postCB = pe_var_postCB;
  pe_register_vtbl(vt);
}

