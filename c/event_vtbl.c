static int NextID = 0;
static pe_ring AllEvents;
static struct pe_event_vtbl pe_event_base_vtbl;

static void
pe_event_init(pe_event *ev)
{
  SV *sv;
  SV *tmp = newSV(40);
  SAVEFREESV(tmp);
  assert(ev);
  assert(ev->vtbl);
  if (!ev->vtbl->stash)
    croak("sub-class VTBL must have a stash (doesn't)");
  ev->stash = ev->vtbl->stash;
  PE_RING_INIT(&ev->all, ev);
  PE_RING_UNSHIFT(&ev->all, &AllEvents);
  PE_RING_INIT(&ev->que, ev);
  EvFLAGS(ev) = 0;
  EvINVOKE1_on(ev);
  ev->FALLBACK = 0;
  NextID = (NextID+1) & 0x7fff; /* make it look like the kernel :-, */
  ev->id = NextID;
  ev->refcnt = 0;  /* maybe can remove later? XXX */
  ev->desc = newSVpvf("Event-0x%x", ev);
  ev->count = 0;
  ev->priority = QUEUES;
  ev->perl_callback[0] = 0;
  ev->perl_callback[1] = 0;
  ev->c_callback = 0;
  pe_stat_init(&ev->stats);
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
  case 'f':
    if (len == 5 && memEQ(key, "flags", 5)) {
      ret = sv_2mortal(newSViv(ev->flags));
      break;
    }
    break;
  case 'i':
    if (len == 2 && memEQ(key, "id", 2)) {
      ret = sv_2mortal(newSViv(ev->id));
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
    if (len == 6 && memEQ(key, "repeat", 6)) {
      ret = boolSV(EvREPEAT(ev));
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
    /* move after REFCNT_inc XXX */
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
    if (len == 5 && memEQ(key, "count", 5))
      croak("'count' is read-only");
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
  case 'f':
    if (len == 5 && memEQ(key, "flags", 5)) {
      /* can only write to some flags */
      ev->flags = ((ev->flags & PE_INTERNAL_FLAGS) |
		   (SvIV(nval) & ~PE_INTERNAL_FLAGS));
    }
    break;
  case 'i':
    if (len == 2 && memEQ(key, "id", 2))
      croak("'id' is read-only");
    break;
  case 'p':
    if (len == 8 && memEQ(key, "priority",8)) {
      ok=1;
      if (ev->priority != SvIV(nval)) {
	int qued = EvQUEUED(ev);
	if (qued) dequeEvent(ev);
	ev->priority = SvIV(nval);
	if (qued) queueEvent(ev, 0);
      }
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

static void pe_event_invoke(pe_event *ev)     /* can destroy event! */
{
  struct timeval start_tm;
  int flags = G_VOID;
  int debug = SvIVX(DebugLevel) + EvDEBUG(ev);
  assert(!EvSUSPEND(ev));
  if (debug >= 2)
    warn("Event: invoking '%s'\n", SvPV(ev->desc, na));
  if (EvRUNNING(ev)) {
    warn("Event: '%s' invoked recursively (skipped)\n");
    return;
  }
  if (Stats)
    gettimeofday(&start_tm, 0);
  EvRUNNING_on(ev);
  /* can't make event specific because of c_callback XXX */
  if (SvIVX(Eval) || EvDEBUG(ev))
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
    if ((flags & G_EVAL) && SvTRUE(ERRSV)) {
      SV *eval = perl_get_sv("Event::DIED", 1);
      dSP;
      PUSHMARK(SP);
      XPUSHs(sv_2mortal(event_2sv(ev)));
      XPUSHs(sv_mortalcopy(ERRSV));
      PUTBACK;
      perl_call_sv(eval, G_EVAL|G_KEEPERR|G_DISCARD);
    }
    FREETMPS;
    LEAVE;
  } else if (ev->c_callback) {
    (*ev->c_callback)(ev->ext_data);
  } else {
    croak("No callback for event '%s'", SvPV(ev->desc,na));
  }
  ev->count = 0;
  EvRUNNING_off(ev);
  if (Stats) {
    struct timeval done_tm;
    gettimeofday(&done_tm, 0);
    pe_stat_record(&ev->stats, (done_tm.tv_sec - start_tm.tv_sec +
				(done_tm.tv_usec - start_tm.tv_usec)/1000000.0));
  }
  if (debug >= 3)
    warn("Event: completed '%s'\n", SvPV(ev->desc, na));

  if (EvINVOKE1(ev)) {
    /* optimized for non-repeating events [DEFAULT] */
    if (EvREPEAT(ev))
      (*ev->vtbl->start)(ev, 1);
    else if (EvCANDESTROY(ev))
      (*ev->vtbl->dtor)(ev);
  }
  else {
    /* optimized for repeating events */
    if (!EvREPEAT(ev))
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
  pe_event_nomethod(ev,"start");
}

static void
pe_event_stop(pe_event *ev)
{ pe_event_nomethod(ev,"stop"); }

static void
boot_pe_event()
{
  static char *keylist[] = {
    "id",
    "repeat",
    "priority",
    "desc",
    "count",
    "callback",
    "debug",
    "flags"
  };
  HV *stash = gv_stashpv("Event", 1);
  struct pe_event_vtbl *vt;
  PE_RING_INIT(&AllEvents, 0);
  vt = &pe_event_base_vtbl;
  vt->up = 0;
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
  vt->start = pe_event_start;
  vt->stop = pe_event_stop;

  newCONSTSUB(stash, "ACTIVE", newSViv(PE_ACTIVE));
  newCONSTSUB(stash, "SUSPEND", newSViv(PE_SUSPEND));
  newCONSTSUB(stash, "QUEUED", newSViv(PE_QUEUED));
  newCONSTSUB(stash, "RUNNING", newSViv(PE_RUNNING));
  newCONSTSUB(stash, "INVOKE1", newSViv(PE_INVOKE1));
}

static void
pe_register_vtbl(pe_event_vtbl *vt)
{
  /* why?  dunno */
}

void pe_event_cancel(pe_event *ev)
{
  if (!EvSUSPEND(ev)) {
    (*ev->vtbl->stop)(ev);
    if (EvQUEUED(ev))
      dequeEvent(ev);
  }
  if (EvCANDESTROY(ev))
    (*ev->vtbl->dtor)(ev);
}

void pe_event_suspend(pe_event *ev)
{
  if (EvSUSPEND(ev))
    return;
  if (EvACTIVE(ev)) {
    (*ev->vtbl->stop)(ev);
    EvACTIVE_on(ev);
  }
  if (EvQUEUED(ev)) {
    dequeEvent(ev);
    EvQUEUED_on(ev);
  }
  EvSUSPEND_on(ev);
}

void pe_event_resume(pe_event *ev)
{
  if (!EvSUSPEND(ev))
    return;
  EvSUSPEND_off(ev);
  if (EvACTIVE(ev)) {
    EvACTIVE_off(ev);
    (*ev->vtbl->start)(ev, 0);
  }
  if (EvQUEUED(ev)) {
    EvQUEUED_off(ev);
    queueEvent(ev, 0);
  }
}

void pe_event_now(pe_event *ev)
{
  if (EvSUSPEND(ev))
    return;
  if (EvACTIVE(ev))
    (*ev->vtbl->stop)(ev);
  queueEvent(ev, 1);
}
