/* 100 levels will trigger a manditory warning from perl */
#define MAX_CB_NEST 95

static double QueueTime[PE_QUEUES];

static pe_cbframe CBFrame[MAX_CB_NEST];
static int CurCBFrame = -1;

pe_event_vtbl event_vtbl, ioevent_vtbl;

static void pe_event_init(pe_event *ev, pe_watcher *wa)
{
  assert(wa);
  ev->up = wa;
  ev->mysv = 0;
  PE_RING_INIT(&ev->peer, ev);
  PE_RING_UNSHIFT(&ev->peer, &wa->events);
  ev->count = 0;
  ev->priority = wa->priority;
}

static void pe_event_dtor(pe_event *ev)
{
    STRLEN n_a;
    pe_watcher *wa = ev->up;
    HV *fb = wa->FALLBACK;
    if (EvDEBUGx(wa) >= 3)
	warn("Event=0x%x '%s' destroyed (SV=0x%x)",
	     ev, SvPV(wa->desc, n_a), ev->mysv? SvRV(ev->mysv) : 0);
    if (ev->mysv) {
	invalidate_sv(ev->mysv);
	ev->mysv=0;
    }
    if (fb) {
	HE *ent;
	hv_iterinit(fb);
	while ((ent = hv_iternext(fb))) {
	    if (HeVAL(ent))
		mg_free(HeVAL(ent));
	}
    } /**/
    ev->up = 0;
    ev->count = 0;
    PE_RING_DETACH(&ev->peer);
    PE_RING_DETACH(&ev->que);
    PE_RING_UNSHIFT(&ev->que, &event_vtbl.freelist);
}

/*****************************************************************/

static pe_event *pe_event_allocate(pe_watcher *wa)
{
  pe_event *ev;
  assert(wa);
  if (EvCLUMPx(wa))
    return (pe_event*) wa->events.next->self;
  if (PE_RING_EMPTY(&event_vtbl.freelist)) {
    New(PE_NEWID, ev, 1, pe_event);
    ev->vtbl = &event_vtbl;
    PE_RING_INIT(&ev->que, ev);
  }
  else {
    PE_RING_POP(&event_vtbl.freelist, ev);
  }
  pe_event_init(ev, wa);
  return ev;
}

EKEYMETH(_event_hits)
{
  if (!nval) {
    dSP;
    XPUSHs(sv_2mortal(newSViv(ev->count)));
    PUTBACK;
  } else
    croak("'e_hits' is read-only");
}

/*------------------------------------------------------*/

static pe_event *pe_ioevent_allocate(pe_watcher *wa)
{
  pe_ioevent *ev;
  assert(wa);
  if (EvCLUMPx(wa))
    return (pe_event*) wa->events.next->self;
  if (PE_RING_EMPTY(&ioevent_vtbl.freelist)) {
    New(PE_NEWID, ev, 1, pe_ioevent);
    ev->base.vtbl = &ioevent_vtbl;
    PE_RING_INIT(&ev->base.que, ev);
  }
  else {
    PE_RING_POP(&ioevent_vtbl.freelist, ev);
  }
  pe_event_init(&ev->base, wa);
  ev->got = 0;
  return &ev->base;
}

EKEYMETH(_event_got)
{
  pe_ioevent *io = (pe_ioevent *)ev;
  if (!nval) {
    dSP;
    XPUSHs(sv_2mortal(events_mask_2sv(io->got)));
    PUTBACK;
  } else
    croak("'e_got' is read-only");
}

/*------------------------------------------------------*/

static void pe_event_postCB(pe_cbframe *fp)
{
  pe_watcher *wa = fp->ev->up;
  (*fp->ev->vtbl->dtor)(fp->ev);
  --CurCBFrame;
  if (EvACTIVE(wa) && EvINVOKE1(wa) && EvREPEAT(wa))
    pe_watcher_on(wa, 1);
  if (Estat.on) {
    if (fp->stats) {
      Estat.abort(fp->stats, wa);
      fp->stats = 0;
    }
    if (CurCBFrame >= 0)
      Estat.resume((CBFrame + CurCBFrame)->stats);
  }
  if (EvCANDESTROY(wa))
    (*wa->vtbl->dtor)(wa);
}

static void pe_callback_died(pe_cbframe *fp)
{
  STRLEN n_a;
  pe_watcher *wa = fp->ev->up;
  SV *eval = perl_get_sv("Event::DIED", 1);
  SV *err = sv_true(ERRSV)? sv_mortalcopy(ERRSV) : sv_2mortal(newSVpv("?",0));
  dSP;
  if (EvDEBUGx(wa) >= 4)
    warn("Event: '%s' died with: %s\n", SvPV(wa->desc,n_a), SvPV(ERRSV,n_a));
  PUSHMARK(SP);
  XPUSHs(event_2sv(fp->ev));
  XPUSHs(err);
  PUTBACK;
  perl_call_sv(eval, G_EVAL|G_DISCARD);
  if (sv_true(ERRSV)) {
    warn("Event: '%s' died and then $Event::DIED died with: %s\n",
	 SvPV(wa->desc,n_a), SvPV(ERRSV,n_a));
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

  fp = CBFrame + CurCBFrame;
  ev = fp->ev->up;
  if (ev->running == fp->run_id) {
    if (Estat.on)
      Estat.suspend(fp->stats);
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

static void pe_event_invoke(pe_event *ev)
{
  STRLEN n_a;
  pe_watcher *wa = ev->up;
  struct pe_cbframe *frp;

  pe_check_recovery();

  /* SETUP */
  ENTER;
  SAVEINT(wa->running);
  PE_RING_DETACH(&ev->peer);  /* disallow clumping after this point! */
  frp = &CBFrame[++CurCBFrame];
  frp->ev = ev;
  frp->run_id = ++wa->running;
  if (Estat.on)
    frp->stats = Estat.enter(CurCBFrame);
  assert(ev->priority >= 0 && ev->priority < PE_QUEUES);
  QueueTime[ev->priority] = wa->cbtime = EvNOW(EvCBTIME(wa));
  /* SETUP */

  if (CurCBFrame+1 >= MAX_CB_NEST) {
    SV *exitL = perl_get_sv("Event::ExitLevel", 1);
    sv_setiv(exitL, 0);
    croak("Deep recursion detected; invoking unloop_all()\n");
  }

  if (EvDEBUGx(wa) >= 2)
    warn("Event: [%d]invoking '%s' (prio %d)\n",
	 CurCBFrame, SvPV(wa->desc,n_a),ev->priority);

  if (!PE_RING_EMPTY(&Callback)) pe_map_check(&Callback);

  if (EvPERLCB(wa)) {
    SV *cb = SvRV((SV*)wa->callback);
    int pcflags = G_VOID | (SvIVX(Eval)? G_EVAL : 0);
    dSP;
    SV *evsv = event_2sv(ev);
    if (SvTYPE(cb) == SVt_PVCV) {
      PUSHMARK(SP);
      XPUSHs(evsv);
      PUTBACK;
      perl_call_sv(wa->callback, pcflags);
    } else {
      AV *av = (AV*)cb;
      dSP;
      assert(SvTYPE(cb) == SVt_PVAV);
      PUSHMARK(SP);
      XPUSHs(*av_fetch(av, 0, 0));
      XPUSHs(evsv);
      PUTBACK;
      perl_call_method(SvPV(*av_fetch(av, 1, 0),n_a), pcflags);
    }
    if ((pcflags & G_EVAL) && SvTRUE(ERRSV))
      pe_callback_died(frp);
  } else if (wa->callback) {
    (* (void(*)(void*,pe_event*)) wa->callback)(wa->ext_data, ev);
  } else {
    croak("No callback for event '%s'", SvPV(wa->desc,n_a));
  }

  LEAVE;

  if (Estat.on) {
    Estat.commit(frp->stats, wa);
    frp->stats=0;
  }
  if (EvDEBUGx(wa) >= 3)
    warn("Event: completed '%s'\n", SvPV(wa->desc, n_a));

  pe_event_postCB(frp);
}

static void pe_event_FETCH(void *vptr, SV *svkey)
{
  pe_event *ev = (pe_event*) vptr;
  pe_watcher *wa = ev->up;
  SV **mp;
  STRLEN len;
  char *key = SvPV(svkey, len);
  if (!len) return;
  mp = hv_fetch(ev->vtbl->keymethod, key, len, 0);
  if (mp) {
    assert(*mp && SvIOK(*mp));
    ((void(*)(pe_event*,SV*)) SvIVX(*mp))(ev,0);
    return;
  }
  assert(wa);
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
  if (!len) return;
  mp = hv_fetch(ev->vtbl->keymethod, key, len, 0);
  if (mp) {
    assert(*mp && SvIOK(*mp));
    ((void(*)(pe_event*,SV*)) SvIVX(*mp))(ev,nval);
    return;
  }
  assert(wa);
  mp = hv_fetch(wa->vtbl->keymethod, key, len, 0);
  if (mp) {
    assert(*mp && SvIOK(*mp));
    ((void(*)(pe_watcher*,SV*)) SvIVX(*mp))(wa,nval);
  }
  else
    pe_watcher_STORE_FALLBACK(wa, svkey, nval);
}

static void pe_event_FIRSTKEY(void *vptr);

static void pe_event_NEXTKEY(void *vptr)
{
  STRLEN n_a;
  pe_event *ev = (pe_event*) vptr;
  pe_watcher *wa = ev->up;
  HE *he=0;
  switch (wa->iter) {
  case PE_ITER_EVENT:
    he = hv_iternext(ev->vtbl->keymethod);
    if (he)
      break;
    else
      wa->iter = PE_ITER_WATCHER;
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

static void pe_event_DELETE(void *vptr, SV *svkey)
{
  STRLEN n_a;
  pe_event *ev = (pe_event*) vptr;
  pe_watcher *wa = ev->up;
  SV *ret;
  if (hv_exists_ent(ev->vtbl->keymethod, svkey, 0))
    croak("Cannot delete key '%s'", SvPV(svkey,n_a));
  if (hv_exists_ent(wa->vtbl->keymethod, svkey, 0))
    croak("Cannot delete key '%s'", SvPV(svkey,n_a));
  if (!wa->FALLBACK)
    return;
  ret = hv_delete_ent(wa->FALLBACK, svkey, 0, 0);
  if (ret && GIMME_V != G_VOID) {
    dSP;
    XPUSHs(ret);  /* already mortalized */
    PUTBACK;
  }
}

static int pe_event_EXISTS(void *vptr, SV *svkey)
{
  pe_event *ev = (pe_event*) vptr;
  pe_watcher *wa = ev->up;
  if (hv_exists_ent(ev->vtbl->keymethod, svkey, 0))
    return 1;
  if (hv_exists_ent(wa->vtbl->keymethod, svkey, 0))
    return 1;
  if (!wa->FALLBACK)
    return 0;
  return hv_exists_ent(wa->FALLBACK, svkey, 0);
}

static void boot_pe_event()
{
  pe_event_vtbl *vt;

  vt = &event_vtbl;
  vt->base.is_event = 1;
  vt->base.Fetch = pe_event_FETCH;
  vt->base.Store = pe_event_STORE;
  vt->base.Firstkey = pe_event_FIRSTKEY;
  vt->base.Nextkey = pe_event_NEXTKEY;
  vt->base.Delete = pe_event_DELETE;
  vt->base.Exists = pe_event_EXISTS;
  vt->new_event = pe_event_allocate;
  vt->dtor = pe_event_dtor;
  vt->keymethod = newHV();
  hv_store(vt->keymethod, "e_hits", 6, newSViv((IV)_event_hits), 0);
  /* priority? XXX */
  PE_RING_INIT(&vt->freelist, 0);

  vt = &ioevent_vtbl;
  memcpy(vt, &event_vtbl, sizeof(pe_event_vtbl));
  vt->keymethod = newHVhv(event_vtbl.keymethod);
  hv_store(vt->keymethod, "e_got", 5, newSViv((IV)_event_got), 0);
  vt->new_event = pe_ioevent_allocate;
  vt->dtor = pe_event_dtor;
  PE_RING_INIT(&vt->freelist, 0);

  memset(QueueTime, 0, sizeof(QueueTime));
}
