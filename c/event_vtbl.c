
static pe_ring AllEvents;
static struct pe_event_vtbl pe_event_base_vtbl;

static void
pe_event_init(pe_event *ev)
{
  assert(ev->vtbl);
  if (!ev->vtbl->stash)
    croak("sub-class VTBL must have a stash (doesn't)");
  PE_RING_INIT(&ev->all, ev);
  PE_RING_UNSHIFT(&ev->all, &AllEvents);
  PE_RING_INIT(&ev->que, ev);
  EvFLAGS(ev) = 0;
  ev->FALLBACK = 0;
  ev->refcnt = 0;
  ev->desc = newSVpvf("ANON-0x%x", ev);
  ev->count = 0;
  ev->perl_callback[0] = 0;
  ev->perl_callback[1] = 0;
  ev->c_callback = 0;
  ev->ran = 0;
  ev->elapse_tm = 0;
  ev->total_elapse = 0;
  assert(ev->vtbl->default_priority);
  ev->priority = SvIV(ev->vtbl->default_priority);
  assert(ev->vtbl->instances);
  ++SvIVX(ev->vtbl->instances);
}

static void
pe_event_dtor(pe_event *ev)
{
  int xx;
  if (SvIVX(DebugLevel) + EvDEBUG(ev) >= 3)
    warn("dtor '%s'", SvPV(ev->desc,na));
  PE_RING_DETACH(&ev->all);
  PE_RING_DETACH(&ev->que);
  if (ev->FALLBACK)
    SvREFCNT_dec(ev->FALLBACK);
  if (ev->desc)
    SvREFCNT_dec(ev->desc);
  for (xx=0; xx < 2; xx++) {
    if (ev->perl_callback[xx])
      SvREFCNT_dec(ev->perl_callback[xx]);
  }
  --ev->vtbl->instances;
  safefree(ev);
}

static void
pe_event_FETCH(pe_event *ev, SV *svkey)
{
  SV *ret=0;
  STRLEN len;
  char *key = SvPV(svkey, len);
  if (len && key[0] == '-') { ++key; --len; }
  if (!len) return;
  switch (key[0]) {
  case 'c':
    if (len == 8 && memEQ(key, "callback", 8)) {
      if (ev->perl_callback[0]) {
	if (!ev->perl_callback[1]) {
	  ret = ev->perl_callback[0];
	} else {
	  int xx;
	  AV *av = newAV();
	  for (xx=0; xx < 2; xx++) {
	    av_store(av, xx, SvREFCNT_inc(ev->perl_callback[xx]));
	  }
	  ret = sv_2mortal(newRV_noinc((SV*)av));
	}
      } else if (ev->c_callback) {
	ret = sv_2mortal(newSVpvf("<CFUNC=0x%x EXT=0x%x>",
				  ev->c_callback, ev->ext_data));
      }
      break;
    }
    if (len == 5 && memEQ(key, "count", 5)) {
      ret = sv_2mortal(newSViv(ev->count));
      break;
    }
    break;
  case 'd':
    if (len == 4 && memEQ(key, "desc", 4)) { ret = ev->desc; break; }
    if (len == 5 && memEQ(key, "debug", 5)) { ret = boolSV(EvDEBUG(ev)); break; }
    break;
  case 'e':
    if (len == 9 && memEQ(key, "elapse_tm", 9)) {
      ret = sv_2mortal(newSVnv(ev->elapse_tm));
      break;
    }
    break;
  case 'p':
    if (len == 8 && memEQ(key, "priority", 8)) {
      ret = sv_2mortal(newSViv(ev->priority));
      break;
    }
    break;
  case 'r':
    if (len == 3 && memEQ(key, "ran", 3)) {
      ret = sv_2mortal(newSViv(ev->ran));
      break;
    }
    if (len == 6 && memEQ(key, "repeat", 6)) {
      ret = boolSV(EvREPEAT(ev));
      break;
    }
    break;
  case 't':
    if (len == 12 && memEQ(key, "total_elapse", 12)) {
      ret = sv_2mortal(newSVnv(ev->total_elapse));
      break;
    }
    break;
  }
  if (!ret && ev->FALLBACK) {
    HE *he = hv_fetch_ent(ev->FALLBACK, svkey, 0, 0);
    if (he)
      ret = HeVAL(he);
  }
  if (ret) {
    dSP;
    XPUSHs(ret);
    PUTBACK;
  }
}

static void
pe_event_STORE(pe_event *ev, SV *svkey, SV *nval)
{
  int xx;
  STRLEN len;
  char *key = SvPV(svkey, len);
  int ok=0;
  if (len && key[0] == '-') { ++key; --len; }
  if (!len) return;
  switch (key[0]) {
  case 'c':
    if (len == 8 && memEQ(key, "callback", 8)) { ok=1;
      for (xx=0; xx < 2; xx++) {
	if (ev->perl_callback[xx]) {
	  SvREFCNT_dec(ev->perl_callback[xx]);
	  ev->perl_callback[xx] = 0;
	}
      }
      if (!SvOK(nval)) {
	/*ok*/
      } else if (SvROK(nval) && SvTYPE(SvRV(nval)) == SVt_PVAV) {
	AV *av = (AV*)SvRV(nval);
	if (av_len(av) != 1) croak("Expecting [$class,$method]");
	for (xx=0; xx < 2; xx++) {
	  SV **tmp = av_fetch(av,xx,0);
	  if (!tmp || !SvOK(*tmp)) {
	    if (tmp) sv_dump(*tmp);
	    croak("Bad arg at %d", xx);
	  }
	  ev->perl_callback[xx] = SvREFCNT_inc(*tmp);
	}
      } else {
	ev->perl_callback[0] = SvREFCNT_inc(nval);
      }
      break;
    }
    if (len == 5 && memEQ(key, "count", 5)) {
      ok=1; ev->count = SvIV(nval); break;
    }
    break;
  case 'd':
    if (len == 4 && memEQ(key, "desc", 4)) {
      ok=1;
      sv_setsv(ev->desc, nval);
      break;
    }
    if (len == 5 && memEQ(key, "debug",5)) {
      ok=1;
      if (SvTRUEx(nval)) EvDEBUG_on(ev); else EvDEBUG_off(ev);
      break;
    }
    break;
  case 'p':
    if (len == 8 && memEQ(key, "priority",8)) {
      ok=1;
      ev->priority = SvIV(nval);
      break;
    }
    break;
  case 'r':
    if (len == 6 && memEQ(key, "repeat",6)) {
      ok=1;
      if (SvTRUEx(nval)) EvREPEAT_on(ev); else EvREPEAT_off(ev);
      break;
    }
    break;
  }
  if (!ok) {
    if (!ev->FALLBACK)
      ev->FALLBACK = newHV();
    hv_store_ent(ev->FALLBACK, svkey, SvREFCNT_inc(nval), 0);
  }
}

static void
pe_event_DELETE(pe_event *ev, SV *svkey)
{
  char *key = SvPV(svkey, na);
  SV *ret;
  pe_event_vtbl *vt = ev->vtbl;
  int at = 0;
  while (vt) {
    if (at < vt->keys) {
      if (strEQ(key, vt->keylist[at]))
	croak("Hash field '%s' cannot be deleted", key);
      ++at;
    } else {
      at = 0;
      vt = vt->up;
    }
  }
  if (!ev->FALLBACK)
    return;
  ret = hv_delete_ent(ev->FALLBACK, svkey, 0, 0);
  if (ret) {
    dSP;
    XPUSHs(sv_2mortal(ret));
    PUTBACK;
  }
}

static int
pe_event_EXISTS(pe_event *ev, SV *svkey)
{
  char *key = SvPV(svkey, na);
  pe_event_vtbl *vt = ev->vtbl;
  int at = 0;
  while (vt) {
    if (at < vt->keys) {
      if (strEQ(key, vt->keylist[at]))
	return 1;
      ++at;
    } else {
      at = 0;
      vt = vt->up;
    }
  }
  if (!ev->FALLBACK)
    return 0;
  return hv_exists_ent(ev->FALLBACK, svkey, 0);
}

static void
pe_event_FIRSTKEY(pe_event *ev)
{
  ev->iter = 0;
  if (ev->FALLBACK)
    hv_iterinit(ev->FALLBACK);
  (*ev->vtbl->NEXTKEY)(ev);
}

static void
pe_event_NEXTKEY(pe_event *ev)
{
  pe_event_vtbl *vt = ev->vtbl;
  int at = ev->iter++;
  while (vt) {
    if (at < vt->keys) {
      dSP;
      XPUSHs(sv_2mortal(newSVpv(vt->keylist[at], 0)));
      PUTBACK;
      return;
    } else {
      at -= vt->keys;
      vt = vt->up;
    }
  }
  if (ev->FALLBACK) {
    HE *entry = hv_iternext(ev->FALLBACK);
    if (entry) {
      dSP;
      XPUSHs(hv_iterkeysv(entry));
      PUTBACK;
    }
  }
}

static void
pe_invoke_callback(pe_event *ev)
{
  struct timeval start_tm;
  int flags = G_VOID;
  int debug = SvIVX(DebugLevel) + EvDEBUG(ev);
  if (debug) {
    if (debug >= 2) {
      warn("Event: invoking %s\n", SvPV(ev->desc, na));
    }
  }
  if (EvRUNNING(ev)) {
    warn("Event: '%s' invoked recursively (ignored)\n");
    return;
  }
  if (SvIVX(Stats))
    gettimeofday(&start_tm, 0);
  EvRUNNING_on(ev);
  if (SvIVX(Eval) + EvDEBUG(ev))
    flags |= G_EVAL;
  /* ignore return value? */
  if (ev->perl_callback[0]) {
    dSP;
    ENTER;
    SAVETMPS;
    if (!ev->perl_callback[1]) {
      PUSHMARK(SP);
      XPUSHs(sv_2mortal(event_2sv(ev)));
      PUTBACK;
      perl_call_sv(ev->perl_callback[0], flags);
    } else {
      dSP;
      PUSHMARK(SP);
      XPUSHs(ev->perl_callback[0]);
      XPUSHs(sv_2mortal(event_2sv(ev)));
      PUTBACK;
      perl_call_method(SvPV(ev->perl_callback[1],na), flags);
    }
    if (flags & G_EVAL && SvTRUE(ERRSV)) {
      warn("Event: fatal error trapped in '%s' callback: %s",
	   SvPV(ev->desc,na), SvPV(ERRSV, na));
    }
    FREETMPS;
    LEAVE;
  } else if (ev->c_callback) {
    (*ev->c_callback)(ev->ext_data);
  } else {
    croak("No callback");
  }
  ev->count = 0;
  EvRUNNING_off(ev);
  if (SvIVX(Stats)) { /*REVISIT XXX*/
    struct timeval done_tm;
    gettimeofday(&done_tm, 0);
    ++ev->ran;
    ev->elapse_tm = (done_tm.tv_sec - start_tm.tv_sec +
		     (done_tm.tv_usec - start_tm.tv_usec) / 1000000);
    ev->total_elapse += ev->elapse_tm;
  }
  if (debug >= 3)
    warn("Event: completed %s\n", SvPV(ev->desc, na));
}

static void
pe_event_invoke_once(pe_event *ev)
{
  /* optimized for non-repeating events [DEFAULT] */
  pe_invoke_callback(ev);
  EvACTIVE_off(ev);
  if (EvREPEAT(ev))
    (*ev->vtbl->start)(ev, 1);
  else if (EvCANDESTROY(ev))
    (*ev->vtbl->dtor)(ev);
}

static void
pe_event_invoke_repeat(pe_event *ev)
{
  /* optimized for repeating events */
  pe_invoke_callback(ev);
  if (!EvREPEAT(ev)) {
    (*ev->vtbl->stop)(ev);
    if (EvCANDESTROY(ev))
      (*ev->vtbl->dtor)(ev);
  }
}

static void
pe_event_nomethod(pe_event *ev, char *meth)
{
  HV *stash = ev->vtbl->stash;
  assert(stash);
  croak("%s::%s is missing", HvNAME(stash), meth);
}

static void
pe_event_start(pe_event *ev, int repeat)
{
  EvSUSPEND_off(ev);
  pe_event_nomethod(ev,"start");
}

static void
pe_event_stop(pe_event *ev)
{ pe_event_nomethod(ev,"stop"); }

static void
boot_pe_event()
{
  static char *keylist[] = {
    "repeat",
    "priority",
    "desc",
    "count",
    "callback",
    "debug",
    "ran",
    "elapse_tm",
    "total_elapse"
  };
  struct pe_event_vtbl *vt;
  PE_RING_INIT(&AllEvents, 0);
  vt = &pe_event_base_vtbl;
  vt->up = 0;
  vt->instances = 0;
  vt->default_priority = 0;
  vt->stash = 0;
  vt->dtor = pe_event_dtor;
  vt->FETCH = pe_event_FETCH;
  vt->STORE = pe_event_STORE;
  vt->DELETE = pe_event_DELETE;
  vt->EXISTS = pe_event_EXISTS;
  vt->keys = sizeof(keylist)/sizeof(char*);
  vt->keylist = keylist;
  vt->FIRSTKEY = pe_event_FIRSTKEY;
  vt->NEXTKEY = pe_event_NEXTKEY;
  vt->invoke = pe_event_invoke_once;
  vt->start = pe_event_start;
  vt->stop = pe_event_stop;
}

static void
pe_register_vtbl(pe_event_vtbl *vt)
{
  SV *tmp = newSV(40);
  SAVEFREESV(tmp);

  sv_setpv(tmp, HvNAME(vt->stash));
  sv_catpv(tmp, "::Count");
  vt->instances = SvREFCNT_inc(perl_get_sv(SvPV(tmp,na), 1));
  sv_setiv(vt->instances, 0);
  SvREADONLY_on(vt->instances);
  
  sv_setpv(tmp, HvNAME(vt->stash));
  sv_catpv(tmp, "::DefaultPriority");
  vt->default_priority = SvREFCNT_inc(perl_get_sv(SvPV(tmp,na), 1));
  sv_setiv(vt->default_priority, PE_PRIO_NORMAL);
}

void pe_event_cancel(pe_event *ev)
{
  (*ev->vtbl->stop)(ev);
  if (EvQUEUED(ev)) {
    PE_RING_DETACH(&ev->que);
    EvQUEUED_off(ev);
  }
  if (EvCANDESTROY(ev))
    (*ev->vtbl->dtor)(ev);
}

void pe_event_suspend(pe_event *ev)
{
  if (!EvSUSPEND(ev)) {
    (*ev->vtbl->stop)(ev);
    EvSUSPEND_on(ev);
  }
}

void pe_event_now(pe_event *ev)
{
  if (EvACTIVE(ev))
    (*ev->vtbl->stop)(ev);
  queueEvent(ev, 1);
}
