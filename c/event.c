#define MAX_CB_NEST 95 /* 100 levels triggers manditory warning from perl */

static pe_cbframe CBFrame[MAX_CB_NEST];
static int CurCBFrame = -1;

pe_event_vtbl event_vtbl, ioevent_vtbl;

static pe_event *pe_event_allocate(pe_watcher *wa)
{
  pe_event *ev;
  assert(wa);
  if (EvCLUMP(wa))
    return wa->ev1;
  if (PE_RING_EMPTY(&event_vtbl.freelist)) {
    New(PE_NEWID, ev, 1, pe_event);
    ev->vtbl = &event_vtbl;
    PE_RING_INIT(&ev->que, ev);
  }
  else {
    PE_RING_POP(&event_vtbl.freelist, ev);
  }
  ++wa->refcnt;
  ev->up = wa;
  ev->count = 0;
  ev->priority = wa->priority;
  wa->ev1 = ev;
  return ev;
}

static void pe_event_dtor(pe_event *ev)
{
  if (ev->up->ev1 == ev)
    ev->up->ev1 = 0;
  ev->count = 0;
  --ev->up->refcnt; /* try destroy XXX */
  PE_RING_UNSHIFT(&ev->que, &event_vtbl.freelist);
}

EKEYMETH(_event_count)
{
  if (!nval) {
    dSP;
    XPUSHs(sv_2mortal(newSViv(ev->count)));
    PUTBACK;
  } else
    croak("'count' is read-only");
}

/*------------------------------------------------------*/

static pe_event *pe_ioevent_allocate(pe_watcher *wa)
{
  pe_ioevent *ev;
  assert(wa);
  if (EvCLUMP(wa))
    return wa->ev1;
  if (PE_RING_EMPTY(&ioevent_vtbl.freelist)) {
    New(PE_NEWID, ev, 1, pe_ioevent);
    ev->base.vtbl = &ioevent_vtbl;
    PE_RING_INIT(&ev->base.que, ev);
  }
  else {
    PE_RING_POP(&ioevent_vtbl.freelist, ev);
  }
  ++wa->refcnt;
  ev->base.up = wa;
  ev->base.count = 0;
  ev->base.priority = wa->priority;
  ev->got = 0;
  wa->ev1 = (pe_event*) ev;
  return (pe_event*) ev;
}

static void pe_ioevent_dtor(pe_event *ev)
{
  if (ev->up->ev1 == ev)
    ev->up->ev1 = 0;
  ev->count = 0;
  --ev->up->refcnt; /* try destroy XXX */
  PE_RING_UNSHIFT(&ev->que, &ioevent_vtbl.freelist);
}

EKEYMETH(_event_got)
{
  pe_ioevent *io = (pe_ioevent *)ev;
  if (!nval) {
    dSP;
    XPUSHs(sv_2mortal(events_mask_2sv(io->got)));
    PUTBACK;
  } else
    croak("'got' is read-only");
}

/*------------------------------------------------------*/

static void pe_event_postCB(pe_cbframe *fp)
{
  (*fp->ev->vtbl->dtor)(fp->ev);
  --CurCBFrame;
  /* try to delete event too XXX */
}

static void pe_callback_died(pe_cbframe *fp)
{
  pe_watcher *wa = fp->ev->up;
  SV *eval = perl_get_sv("Event::DIED", 1);
  SV *err = sv_true(ERRSV)? sv_mortalcopy(ERRSV) : sv_2mortal(newSVpv("?",0));
  dSP;
  if (EvDEBUGx(wa) >= 3)
    warn("Event: '%s' died with: %s\n",
	 SvPV(wa->desc,PL_na), SvPV(ERRSV,PL_na));
  PUSHMARK(SP);
  XPUSHs(sv_2mortal(watcher_2sv(wa)));
  XPUSHs(err);
  PUTBACK;
  perl_call_sv(eval, G_EVAL|G_DISCARD);
  if (sv_true(ERRSV)) {
    warn("Event: '%s' died and then $Event::DIED died with: %s\n",
	 SvPV(wa->desc,PL_na), SvPV(ERRSV,PL_na));
    sv_setpv(ERRSV, "");
  }
}

static void _resume_watcher(void *vp)
{
  pe_watcher *wa = (pe_watcher *)vp;
  pe_watcher_resume(wa);
}

static void pe_check_recovery()
{
  pe_watcher *ev;
  /* NO ASSERTIONS HERE!  EVAL CONTEXT VERY MESSY */
  int alert;
  struct pe_cbframe *fp;
  if (CurCBFrame < 0)
    return;

  /* always invalidate most recent callback */
  fp = CBFrame + CurCBFrame;
  ev = fp->ev->up;
  if (ev->running == fp->run_id) {
    if (!EvREENTRANT(ev) && EvREPEAT(ev) && !EvSUSPEND(ev)) {
      /* temporarily suspend non-reentrant watcher until callback is
	 finished! */
      pe_watcher_suspend(ev);
      SAVEDESTRUCTOR(_resume_watcher, ev);
    }
    return;
  }

  /* exception detected; alert the militia! */
  alert=0;
  while (CurCBFrame >= 0) {
    fp = CBFrame + CurCBFrame;
    if (fp->ev->up->running == fp->run_id)
      break;
    if (!alert) {
      alert=1;
      pe_callback_died(fp);
    }
    pe_event_postCB(fp);
  }
  if (!alert)
    warn("Event: don't know where exception occurred");
}

static void pe_event_invoke(pe_event *ev)     /* can destroy event! */
{
  pe_watcher *wa = ev->up;
  struct pe_cbframe *frp;
  struct timeval start_tm;

  /* We could trade memory to preemptively remove suspended
     events from the queue.  Instead we check in various places. XXX */
  assert(!EvSUSPEND(wa));

  pe_check_recovery();
  if (Stats)
    gettimeofday(&start_tm, 0);

  /* SETUP */
  ENTER;
  SAVEINT(wa->running);
  /* can't clump after this point! */
  if (wa->ev1 == ev) wa->ev1 = 0;
  frp = &CBFrame[++CurCBFrame];
  frp->ev = ev;
  frp->run_id = ++wa->running;
  wa->cbtime = EvNOW(EvCBTIME(wa));
  /* SETUP */

  if (CurCBFrame+1 >= MAX_CB_NEST) {
    SV *exitL = perl_get_sv("Event::ExitLevel", 1);
    sv_setiv(exitL, 0);
    croak("Deep recursion detected; invoking unloop_all()\n");
  }

  if (EvDEBUGx(wa) >= 2)
    warn("Event: [%d]invoking '%s' (prio %d)\n",
	 CurCBFrame, SvPV(wa->desc,PL_na),ev->priority);

  if (EvPERLCB(wa)) {
    SV *cb = SvRV((SV*)wa->callback);
    int pcflags = G_VOID | (SvIVX(Eval)? G_EVAL : 0);
    dSP;
    SAVETMPS;
    if (SvTYPE(cb) == SVt_PVCV) {
      PUSHMARK(SP);
      XPUSHs(sv_2mortal(event_2sv(ev)));
      PUTBACK;
      perl_call_sv(wa->callback, pcflags);
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
      pe_callback_died(frp);
    FREETMPS;
  } else if (wa->callback) {
    (* (void(*)(void*,pe_event*)) wa->callback)(wa->ext_data, ev);
  } else {
    croak("No callback for event '%s'", SvPV(wa->desc,PL_na));
  }

  /* clean up */
  LEAVE;
  pe_event_postCB(frp);
  /* clean up */

  if (Stats) {
    struct timeval done_tm;
    gettimeofday(&done_tm, 0);
    if (!wa->stats) {
      New(PE_NEWID, wa->stats, 1, pe_stat);
      pe_stat_init(wa->stats);
    }
    pe_stat_record(wa->stats, (done_tm.tv_sec - start_tm.tv_sec +
			       (done_tm.tv_usec - start_tm.tv_usec)/1000000.0));
  }

  if (EvDEBUGx(wa) >= 3)
    warn("Event: completed '%s'\n", SvPV(wa->desc, PL_na));
  if (EvCANDESTROY(wa))
    (*wa->vtbl->dtor)(wa);
}

static void pe_event_FETCH(void *vptr, SV *svkey)
{
  pe_event *ev = (pe_event*) vptr;
  pe_watcher *wa = ev->up;
  SV **mp;
  STRLEN len;
  char *key = SvPV(svkey, len);
  if (len && key[0] == '-') {
    if (--WarnCounter >= 0) warn("Please remove leading dash '%s'", key);
    ++key; --len;
  }
  if (!len) return;
  mp = hv_fetch(ev->vtbl->keymethod, key, len, 0);
  if (mp) {
    assert(*mp && SvIOK(*mp));
    ((void(*)(pe_event*,SV*)) SvIVX(*mp))(ev,0);
    return;
  }
  mp = hv_fetch(wa->vtbl->keymethod, key, len, 0);
  if (mp) {
    assert(*mp && SvIOK(*mp));
    ((void(*)(pe_watcher*,SV*)) SvIVX(*mp))(wa,0);
    return;
  }
  else if (wa->FALLBACK) {
    SV **svp = hv_fetch(wa->FALLBACK, key, len, 0);
    if (svp) {
      dSP;
      XPUSHs(*svp);
      PUTBACK;
    }
  }
}

static void pe_event_STORE(void *vptr, SV *svkey, SV *nval)
{
  pe_event *ev = (pe_event*) vptr;
  pe_watcher *wa = ev->up;
  SV **mp;
  STRLEN len;
  char *key = SvPV(svkey, len);
  if (len && key[0] == '-') {
    if (--WarnCounter >= 0) warn("Please remove leading dash '%s'", key);
    ++key; --len;
  }
  if (!len) return;
  mp = hv_fetch(ev->vtbl->keymethod, key, len, 0);
  if (mp) {
    assert(*mp && SvIOK(*mp));
    ((void(*)(pe_event*,SV*)) SvIVX(*mp))(ev,nval);
    return;
  }
  mp = hv_fetch(wa->vtbl->keymethod, key, len, 0);
  if (mp) {
    assert(*mp && SvIOK(*mp));
    ((void(*)(pe_watcher*,SV*)) SvIVX(*mp))(wa,nval);
  }
  else {
    if (!wa->FALLBACK)
      wa->FALLBACK = newHV();
    hv_store_ent(wa->FALLBACK, svkey, SvREFCNT_inc(nval), 0);
  }
}

static void pe_event_FIRSTKEY(void *vptr);

static void pe_event_NEXTKEY(void *vptr)
{
  pe_event *ev = (pe_event*) vptr;
  pe_watcher *wa = ev->up;
  HE *he=0;
  switch (wa->iter) {
  case PE_ITER_WATCHER:
    he = hv_iternext(wa->vtbl->keymethod);
    if (he)
      break;
    else
      wa->iter = PE_ITER_FALLBACK;
  case PE_ITER_FALLBACK:
    if (wa->FALLBACK)
      he = hv_iternext(wa->FALLBACK);
    break;
  default:
    pe_event_FIRSTKEY(vptr);
    return;
  }
  if (he) {
    dSP;
    XPUSHs(hv_iterkeysv(he));
    PUTBACK;
  }
}

static void pe_event_FIRSTKEY(void *vptr)
{
  pe_event *ev = (pe_event*) vptr;
  pe_watcher *wa = ev->up;
  wa->iter = PE_ITER_EVENT;
  hv_iterinit(ev->vtbl->keymethod);
  hv_iterinit(wa->vtbl->keymethod);
  if (wa->FALLBACK)
    hv_iterinit(wa->FALLBACK);
  pe_event_NEXTKEY(ev);
}

static void boot_pe_event()
{
  pe_event_vtbl *vt;

  vt = &event_vtbl;
  vt->base.Fetch = pe_event_FETCH;
  vt->base.Store = pe_event_STORE;
  vt->base.Firstkey = pe_event_FIRSTKEY;
  vt->base.Nextkey = pe_event_NEXTKEY;
  vt->new_event = pe_event_allocate;
  vt->dtor = pe_event_dtor;
  vt->keymethod = newHV();
  hv_store(vt->keymethod, "count", 5, newSViv((IV)_event_count), 0);
  /* priority XXX */
  PE_RING_INIT(&vt->freelist, 0);

  vt = &ioevent_vtbl;
  memcpy(vt, &event_vtbl, sizeof(pe_event_vtbl));
  vt->keymethod = newHVhv(event_vtbl.keymethod);
  hv_store(vt->keymethod, "got", 3, newSViv((IV)_event_got), 0);
  vt->new_event = pe_ioevent_allocate;
  vt->dtor = pe_ioevent_dtor;
  PE_RING_INIT(&vt->freelist, 0);
}
