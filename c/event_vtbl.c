static int NextID = 0;
static pe_ring AllEvents;
static struct pe_event_vtbl pe_event_base_vtbl;

#define MAX_CB_NEST 95 /* 100 is already detected by perl itself! */

static pe_cbframe CBFrame[MAX_CB_NEST];
static int CurCBFrame = -1;

static void pe_event_init(pe_event *ev)
{
  assert(ev);
  assert(ev->vtbl);
  if (!ev->vtbl->stash)
    croak("sub-class VTBL must have a stash (doesn't)");
  if (!ev->vtbl->did_require) {
    char *name = HvNAME(ev->vtbl->stash);
    if (memEQ(name, "Event::", 7))
      name += 7;
    ++ev->vtbl->did_require;
    gv_fetchmethod(gv_stashpv("Event", 1), name);
  }
  ev->stash = ev->vtbl->stash;
  PE_RING_INIT(&ev->all, ev);
  PE_RING_UNSHIFT(&ev->all, &AllEvents);
  PE_RING_INIT(&ev->que, ev);
  EvFLAGS(ev) = 0;
  EvINVOKE1_on(ev);
  EvREENTRANT_on(ev);
  ev->FALLBACK = 0;
  NextID = (NextID+1) & 0x7fff; /* make it look like the kernel :-, */
  ev->id = NextID;
  ev->refcnt = 0;  /* maybe can remove later? XXX */
  ev->desc = newSVpvn("??",0);
  ev->running = 0;
  ev->cbtime = 0;
  ev->count = 0;
  ev->priority = PE_QUEUES;
  ev->callback = 0;
  ev->ext_data = 0;
  ev->stats = 0;
}

static void pe_event_dtor(pe_event *ev)
{
  int xx;
  if (SvIVX(DebugLevel) + EvDEBUG(ev) >= 3)
    warn("dtor '%s'", SvPV(ev->desc,PL_na));
  PE_RING_DETACH(&ev->all);
  PE_RING_DETACH(&ev->que);
  if (ev->FALLBACK)
    SvREFCNT_dec(ev->FALLBACK);
  if (ev->desc)
    SvREFCNT_dec(ev->desc);
  if (EvPERLCB(ev))
    SvREFCNT_dec(ev->callback);
  safefree(ev);
}

static void pe_event_FETCH(pe_event *ev, SV *svkey)
{
  SV *ret=0;
  STRLEN len;
  char *key = SvPV(svkey, len);
  if (len && key[0] == '-') {
    if (--WarnCounter >= 0) warn("Please remove leading dash '%s'", key);
    ++key; --len;
  }
  if (!len) return;
  switch (key[0]) {
  case 'c':
    if (len == 8 && memEQ(key, "callback", 8)) {
      if (EvPERLCB(ev)) {
	ret = (SV*) ev->callback;
      } else {
	ret = sv_2mortal(newSVpvf("<FPTR=0x%x EXT=0x%x>",
				  ev->callback, ev->ext_data));
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
      ret = sv_2mortal(newSViv((ev->flags & PE_VISIBLE_FLAGS) |
			       (ev->running? PE_RUNNING : 0) ));
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
    if (len == 9 && memEQ(key, "reentrant", 9)) {
      ret = boolSV(EvREENTRANT(ev));
      break;
    }
    if (len == 6 && memEQ(key, "repeat", 6)) {
      ret = boolSV(EvREPEAT(ev));
      break;
    }
    if (len == 7 && memEQ(key, "running", 7)) {
      ret = sv_2mortal(newSViv(ev->running));
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

static void pe_event_STORE(pe_event *ev, SV *svkey, SV *nval)
{
  int xx;
  STRLEN len;
  char *key = SvPV(svkey, len);
  int ok=0;
  if (len && key[0] == '-') {
    if (--WarnCounter >= 0) warn("Please remove leading dash '%s'", key);
    ++key; --len;
  }
  if (!len) return;
  switch (key[0]) {
  case 'c':
    if (len == 8 && memEQ(key, "callback", 8)) {
      SV *sv;
      SV *old=0;
      ok=1;
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
    if (len == 5 && memEQ(key, "flags", 5))
      croak("'flags' are read-only");
    break;
  case 'i':
    if (len == 2 && memEQ(key, "id", 2))
      croak("'id' is read-only");
    break;
  case 'p':
    if (len == 8 && memEQ(key, "priority",8)) {
      ok=1;
      ev->priority = SvIV(nval);
      break;
    }
    break;
  case 'r':
    if (len == 9 && memEQ(key, "reentrant", 9)) {
      ok=1;
      if (sv_true(nval))
	EvREENTRANT_on(ev);
      else {
	if (ev->running > 1)
	  croak("'reentrant' cannot be turned off while the callback is nested %d times", ev->running);
	EvREENTRANT_off(ev);
      }
      break;
    }
    if (len == 6 && memEQ(key, "repeat",6)) {
      ok=1;
      if (SvTRUEx(nval)) EvREPEAT_on(ev); else EvREPEAT_off(ev);
      break;
    }
    if (len == 7 && memEQ(key, "running", 7)) {
      croak("'running' is read-only");
    }
    break;
  }
  if (!ok) {
    if (!ev->FALLBACK)
      ev->FALLBACK = newHV();
    hv_store_ent(ev->FALLBACK, svkey, SvREFCNT_inc(nval), 0);
  }
}

static void pe_event_DELETE(pe_event *ev, SV *svkey)
{
  char *key = SvPV(svkey, PL_na);
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
  if (ret && GIMME_V != G_VOID) {
    dSP;
    XPUSHs(sv_2mortal(SvREFCNT_inc(ret)));
    PUTBACK;
  }
}

static int pe_event_EXISTS(pe_event *ev, SV *svkey)
{
  char *key = SvPV(svkey, PL_na);
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

static void pe_event_FIRSTKEY(pe_event *ev)
{
  ev->iter = 0;
  if (ev->FALLBACK)
    hv_iterinit(ev->FALLBACK);
  (*ev->vtbl->NEXTKEY)(ev);
}

static void pe_event_NEXTKEY(pe_event *ev)
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

static void pe_event_died(pe_event *ev)
{
  SV *eval = perl_get_sv("Event::DIED", 1);
  SV *err = sv_true(ERRSV)? sv_mortalcopy(ERRSV) : sv_2mortal(newSVpv("?",0));
  dSP;
  if (EvDEBUGx(ev) >= 3)
    warn("Event: '%s' died with: %s\n",
	 SvPV(ev->desc,PL_na), SvPV(ERRSV,PL_na));
  PUSHMARK(SP);
  XPUSHs(sv_2mortal(event_2sv(ev)));
  XPUSHs(err);
  PUTBACK;
  perl_call_sv(eval, G_EVAL|G_DISCARD);
  if (sv_true(ERRSV)) {
    warn("Event: '%s' died and then $Event::DIED died with: %s\n",
	 SvPV(ev->desc,PL_na), SvPV(ERRSV,PL_na));
    sv_setpv(ERRSV, "");
  }
}

static void pe_check_recovery()
{
  pe_event *ev;
  /* NO ASSERTIONS HERE!  EVAL CONTEXT VERY MESSY */
  int alert;
  struct pe_cbframe *fp;
  if (CurCBFrame < 0) {
    if (SvIVX(DebugLevel) >= 3)
      warn("Event: (no nested callback running)\n");
    return;
  }

  /* always invalidate most recent callback */
  fp = CBFrame + CurCBFrame;
  ev = fp->ev;
  if (ev->running == fp->run_id) {
    if (EvREENTRANT(ev)) {
      if (!fp->cbdone)
	(*fp->ev->vtbl->postCB)(fp);
    }
    else {
      if (EvREPEAT(ev) && !EvSUSPEND(ev)) {
	/* temporarily suspend non-reentrant watcher until callback is
	   finished! */
	pe_event_suspend(ev);
	fp->resume = 1;
      }
    }
    if (SvIVX(DebugLevel) + EvDEBUG(ev) >= 3)
      warn("Event: [%d] '%s' okay (resume=%d)\n", CurCBFrame,
	   SvPV(ev->desc,PL_na), fp->resume);
    return;
  }

  /* exception detected; alert the militia! */
  alert=0;
  while (CurCBFrame >= 0) {
    fp = CBFrame + CurCBFrame;
    if (fp->ev->running == fp->run_id)
      break;
    if (!alert) {
      alert=1;
      pe_event_died(fp->ev);
    }
    if (!fp->cbdone)
      (*fp->ev->vtbl->postCB)(fp);
    --CurCBFrame;
  }
  if (!alert)
    warn("Event: don't know where exception occurred");
}

static void pe_event_invoke(pe_event *ev)     /* can destroy event! */
{
  struct pe_cbframe *frp;
  struct timeval start_tm;

  pe_check_recovery();
  if (Stats)
    gettimeofday(&start_tm, 0);

  /* SETUP */
  ENTER;
  SAVEINT(ev->running);
  frp = &CBFrame[++CurCBFrame];
  frp->ev = ev;
  frp->cbdone = 0;
  frp->resume = 0;
  frp->run_id = ++ev->running;
  ev->cbtime = EvNOW(EvCBTIME(ev));
  /* SETUP */

  if (CurCBFrame+1 >= MAX_CB_NEST) {
    SV *exitL = perl_get_sv("Event::ExitLevel", 1);
    sv_setiv(exitL, 0);
    croak("deep recursion detected; invoking unloop_all()\n");
  }

  if (EvPERLCB(ev)) {
    SV *cb = SvRV((SV*)ev->callback);
    int pcflags = G_VOID | (SvIVX(Eval)? G_EVAL : 0);
    dSP;
    SAVETMPS;
    if (SvTYPE(cb) == SVt_PVCV) {
      PUSHMARK(SP);
      XPUSHs(sv_2mortal(event_2sv(ev)));
      PUTBACK;
      perl_call_sv(ev->callback, pcflags);
    } else {
      AV *av = (AV*)cb;
      dSP;
      assert(SvTYPE(cb) == SVt_PVAV);
      PUSHMARK(SP);
      XPUSHs(*av_fetch(av, 0, 0));
      XPUSHs(sv_2mortal(event_2sv(ev)));
      PUTBACK;
      perl_call_method(SvPV(*av_fetch(av, 1, 0),PL_na), pcflags);
    }
    if ((pcflags & G_EVAL) && SvTRUE(ERRSV))
      pe_event_died(ev);
    FREETMPS;
  } else if (ev->callback) {
    (* (void(*)(void*)) ev->callback)(ev->ext_data);
  } else {
    croak("No callback for event '%s'", SvPV(ev->desc,PL_na));
  }

  /* clean up */
  if (!frp->cbdone)
    (*ev->vtbl->postCB)(frp);
  LEAVE;
  --CurCBFrame;
  /* clean up */

  if (Stats) {
    struct timeval done_tm;
    gettimeofday(&done_tm, 0);
    if (!ev->stats) {
      New(PE_NEWID, ev->stats, 1, pe_stat);
      pe_stat_init(ev->stats);
    }
    pe_stat_record(ev->stats, (done_tm.tv_sec - start_tm.tv_sec +
			       (done_tm.tv_usec - start_tm.tv_usec)/1000000.0));
  }

  if (EvDEBUGx(ev) >= 3)
    warn("Event: completed '%s'\n", SvPV(ev->desc, PL_na));
  if (EvCANDESTROY(ev))
    (*ev->vtbl->dtor)(ev);
}

static void pe_event_postCB(pe_cbframe *fp)
{
  pe_event *ev = fp->ev;
  assert(!fp->cbdone);
  fp->cbdone = 1;
  /* clear all fields that are used to indicate status to the callback */
  ev->count = 0;

  if (SvIVX(DebugLevel) + EvDEBUG(ev) >= 3)
    warn("Event: '%s' callback reset; resume=%d\n",
	 SvPV(ev->desc, PL_na), fp->resume);

  if (fp->resume)
    pe_event_resume(ev);
  else if (EvINVOKE1(ev) && EvREPEAT(ev))
    pe_event_start(ev, 1);
}

static void pe_event_nomethod(pe_event *ev, char *meth)
{
  HV *stash = ev->vtbl->stash;
  assert(stash);
  croak("%s::%s is missing", HvNAME(stash), meth);
}

static void pe_event_nostart(pe_event *ev, int repeat)
{ pe_event_nomethod(ev,"start"); }
static void pe_event_nostop(pe_event *ev)
{ pe_event_nomethod(ev,"stop"); }
static void pe_event_alarm(pe_event *ev, pe_timeable *tm)
{ pe_event_nomethod(ev,"alarm"); }

static void boot_pe_event()
{
  static char *keylist[] = {
    "id",
    "repeat",
    "priority",
    "desc",
    "count",
    "callback",
    "debug",
    "flags",
    "running",
    "reentrant"
  };
  HV *stash = gv_stashpv("Event::Watcher", 1);
  struct pe_event_vtbl *vt;
  PE_RING_INIT(&AllEvents, 0);
  vt = &pe_event_base_vtbl;
  vt->up = 0;
  vt->stash = 0;
  vt->did_require = 0;
  vt->dtor = pe_event_dtor;
  vt->FETCH = pe_event_FETCH;
  vt->STORE = pe_event_STORE;
  vt->DELETE = pe_event_DELETE;
  vt->EXISTS = pe_event_EXISTS;
  vt->keys = sizeof(keylist)/sizeof(char*);
  vt->keylist = keylist;
  vt->FIRSTKEY = pe_event_FIRSTKEY;
  vt->NEXTKEY = pe_event_NEXTKEY;
  vt->start = pe_event_nostart;
  vt->stop = pe_event_nostop;
  vt->alarm = pe_event_alarm;
  vt->postCB = pe_event_postCB;
  newCONSTSUB(stash, "ACTIVE", newSViv(PE_ACTIVE));
  newCONSTSUB(stash, "SUSPEND", newSViv(PE_SUSPEND));
  newCONSTSUB(stash, "QUEUED", newSViv(PE_QUEUED));
  newCONSTSUB(stash, "RUNNING", newSViv(PE_RUNNING));
  newCONSTSUB(stash, "R", newSViv(PE_R));
  newCONSTSUB(stash, "W", newSViv(PE_W));
  newCONSTSUB(stash, "E", newSViv(PE_E));
  newCONSTSUB(stash, "T", newSViv(PE_T));
}

static void pe_register_vtbl(pe_event_vtbl *vt)
{
  /* maybe check more stuff? */
  assert(vt->up);
  assert(vt->stash);
}

static void pe_event_now(pe_event *ev)
{
  if (EvSUSPEND(ev)) return;
  EvRUNNOW_on(ev);
  queueEvent(ev, 1);
}

/******************************************
  The following methods change the status flags.  These are the only
  methods that should venture to change these flags!
 */

static void pe_event_cancel(pe_event *ev)
{
  if (EvSUSPEND(ev))
    EvFLAGS(ev) &= ~(PE_SUSPEND|PE_ACTIVE|PE_QUEUED);
  else {
    pe_event_stop(ev);
    if (EvQUEUED(ev))
      dequeEvent(ev);
  }
  if (EvCANDESTROY(ev))
    (*ev->vtbl->dtor)(ev);
}


static void pe_event_suspend(pe_event *ev)
{
  int active, queued;
  if (EvSUSPEND(ev))
    return;
  active = EvACTIVE(ev);
  queued = EvQUEUED(ev);
  if (EvDEBUGx(ev) >= 4)
    warn("Event: suspend '%s'%s%s\n", SvPV(ev->desc,PL_na),
	 active?" ACTIVE":"", queued?" QUEUED":"");
  if (active)
    pe_event_stop(ev);
  if (active || (EvINVOKE1(ev) && EvREPEAT(ev)))
    EvACTIVE_on(ev); /* must happen nowhere else!! */
  if (queued) {
    dequeEvent(ev);
    EvQUEUED_on(ev);
  }
  EvSUSPEND_on(ev); /* must happen nowhere else!! */
}

static void pe_event_resume(pe_event *ev)
{
  int active, queued;
  if (!EvSUSPEND(ev))
    return;
  active = EvACTIVE(ev);
  queued = EvQUEUED(ev);
  if (EvDEBUGx(ev) >= 4)
    warn("Event: resume '%s'%s%s\n", SvPV(ev->desc,PL_na),
	 active?" ACTIVE":"", queued?" QUEUED":"");
  EvFLAGS(ev) &= ~(PE_SUSPEND|PE_ACTIVE|PE_QUEUED);
  if (active)
    pe_event_start(ev, 0);
  if (queued)
    queueEvent(ev, 0);
}

static void pe_event_start(pe_event *ev, int repeat)
{
  if (EvACTIVE(ev) || EvSUSPEND(ev))
    return;
  if (EvDEBUGx(ev) >= 4)
    warn("Event: active ON '%s'\n", SvPV(ev->desc,PL_na));
  EvACTIVE_on(ev); /* must happen nowhere else!! */
  (*ev->vtbl->start)(ev, repeat);
  ++ActiveWatchers;
}

static void pe_event_stop(pe_event *ev)
{
  if (!EvACTIVE(ev) || EvSUSPEND(ev))
    return;
  if (EvDEBUGx(ev) >= 4)
    warn("Event: active OFF '%s'\n", SvPV(ev->desc,PL_na));
  EvACTIVE_off(ev); /* must happen nowhere else!! */
  (*ev->vtbl->stop)(ev);
  --ActiveWatchers;
}

