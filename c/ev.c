/* 100 levels will trigger a manditory warning from perl */
#define MAX_CB_NEST 95

static double QueueTime[PE_QUEUES];

static pe_cbframe CBFrame[MAX_CB_NEST];
static int CurCBFrame = -1;

pe_event_vtbl event_vtbl, ioevent_vtbl;

static void pe_event_init(pe_event *ev, pe_watcher *wa) {
    assert(wa);
    ev->up = wa;
    ++wa->event_counter;
    ev->mysv = 0;
    PE_RING_INIT(&ev->peer, ev);
    PE_RING_UNSHIFT(&ev->peer, &wa->events);
    ev->hits = 0;
    ev->prio = wa->prio;
}

static void pe_event_dtor(pe_event *ev) {
    STRLEN n_a;
    pe_watcher *wa = ev->up;
    HV *fb = wa->FALLBACK;
    if (EvDEBUGx(wa) >= 3)
	warn("Event=0x%x '%s' destroyed (SV=0x%x)",
	     ev,
	     SvPV(wa->desc, n_a),
	     ev->mysv? SvRV(ev->mysv) : 0);
    ev->up = 0;
    ev->mysv = 0;
    ev->hits = 0;
    PE_RING_DETACH(&ev->peer);
    PE_RING_DETACH(&ev->que);
    PE_RING_UNSHIFT(&ev->que, &event_vtbl.freelist);
    --wa->event_counter;
    if (EvCANDESTROY(wa)) /* running */
	(*wa->vtbl->dtor)(wa);
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
    } else {
	pe_ring *lk = event_vtbl.freelist.prev;
	PE_RING_DETACH(lk);
	ev = (pe_event*) lk->self;
    }	
    pe_event_init(ev, wa);
    return ev;
}

EKEYMETH(_event_hits)
{
  if (!nval) {
    dSP;
    XPUSHs(sv_2mortal(newSViv(ev->hits)));
    PUTBACK;
  } else
    croak("'e_hits' is read-only");
}

EKEYMETH(_event_prio)
{
  if (!nval) {
    dSP;
    XPUSHs(sv_2mortal(newSViv(ev->prio)));
    PUTBACK;
  } else
    croak("'e_prio' is read-only");
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
    } else {
	pe_ring *lk = ioevent_vtbl.freelist.prev;
	PE_RING_DETACH(lk);
	ev = (pe_ioevent*) lk->self;
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

static void pe_event_postCB(pe_cbframe *fp) {
    pe_event *ev = fp->ev;
    pe_watcher *wa = ev->up;
    --CurCBFrame;
    if (EvACTIVE(wa) && EvINVOKE1(wa) && EvREPEAT(wa))
	pe_watcher_on(wa, 1);
    if (Estat.on) {
	if (fp->stats) {
	    Estat.scrub(fp->stats, wa);
	    fp->stats = 0;
	}
	if (CurCBFrame >= 0)
	    Estat.resume((CBFrame + CurCBFrame)->stats);
    }
    /* this must be last because it can destroy the watcher */
    if (!ev->mysv)
	(*ev->vtbl->dtor)(ev);
    else {
	SvREFCNT_dec(ev->mysv);
	ev->mysv=0;
    }
}

static void pe_callback_died(pe_cbframe *fp)
{
  dSP;
  STRLEN n_a;
  pe_watcher *wa = fp->ev->up;
  SV *eval = perl_get_sv("Event::DIED", 1);
  SV *err = sv_true(ERRSV)? sv_mortalcopy(ERRSV) : sv_2mortal(newSVpv("?",0));
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

static void pe_event_invoke(pe_event *ev) {
    STRLEN n_a;
    int Dbg;
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
	frp->stats = Estat.enter(CurCBFrame, wa->max_cb_tm);
    assert(ev->prio >= 0 && ev->prio < PE_QUEUES);
    QueueTime[ev->prio] = wa->cbtime = EvNOW(EvCBTIME(wa));
    /* SETUP */

    if (CurCBFrame+1 >= MAX_CB_NEST) {
	SV *exitL = perl_get_sv("Event::ExitLevel", 1);
	sv_setiv(exitL, 0);
	croak("Deep recursion detected; invoking unloop_all()\n");
    }

    Dbg = EvDEBUGx(wa);
    if (Dbg) {
	/*
	SV *cvb = perl_get_sv("Carp::Verbose", 1);
	if (!SvIV(cvb)) {
	    SAVEIV(SvIVX(cvb));
	    SvIVX(cvb) = 1;
	}
	*/

	if (Dbg >= 2)
	    warn("Event: [%d]invoking '%s' (prio %d)\n",
		 CurCBFrame, SvPV(wa->desc,n_a),ev->prio);
    }

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
	    perl_call_sv((SV*) wa->callback, pcflags);
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
	if (SvTRUE(ERRSV)) {
	    if (pcflags & G_EVAL)
		pe_callback_died(frp);
	    else
		sv_setsv(ERRSV, &PL_sv_no);
	}
    } else if (wa->callback) {
	(* (void(*)(void*,pe_event*)) wa->callback)(wa->ext_data, ev);
    } else {
	croak("No callback for event '%s'", SvPV(wa->desc,n_a));
    }

    LEAVE;

    if (Estat.on) {
	if (frp->stats)  /* maybe in transition */
	    Estat.commit(frp->stats, wa);
	frp->stats=0;
    }
    if (Dbg >= 3)
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
      if (--WarnCounter >= 0)
	  warn("Please access '%s' via the method '%s'", key, 2+key);
    assert(*mp && SvIOK(*mp));
    ((void(*)(pe_event*,SV*)) SvIVX(*mp))(ev,0);
    return;
  }
  assert(wa);
  mp = hv_fetch(wa->vtbl->keymethod, key, len, 0);
  if (mp) {
    assert(*mp && SvIOK(*mp));
    if (--WarnCounter >= 0)
	warn("Please access attribute '%s' via the watcher method", key);
    ((void(*)(pe_watcher*,SV*)) SvIVX(*mp))(wa,0);
    return;
  }
  else if (wa->FALLBACK) {
    SV **svp = hv_fetch(wa->FALLBACK, key, len, 0);
    if (svp) {
	dSP;
	if (--WarnCounter >= 0)
	    warn("Please access attribute '%s' via the watcher", key);
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
      if (--WarnCounter >= 0)
	  warn("Please access '%s' via the method '%s'", key, 2+key);
    assert(*mp && SvIOK(*mp));
    ((void(*)(pe_event*,SV*)) SvIVX(*mp))(ev,nval);
    return;
  }
  assert(wa);
  mp = hv_fetch(wa->vtbl->keymethod, key, len, 0);
  if (mp) {
    assert(*mp && SvIOK(*mp));
    if (--WarnCounter >= 0)
	warn("Please access attribute '%s' via the watcher method", key);
    ((void(*)(pe_watcher*,SV*)) SvIVX(*mp))(wa,nval);
  }
  else {
    if (--WarnCounter >= 0)
	warn("Please access attribute '%s' via the watcher", key);
    pe_watcher_STORE_FALLBACK(wa, svkey, nval);
  }
}

static void pe_event_NEXTKEY(void *vptr) {
    pe_event *ev = (pe_event*) vptr;
    HE *he = hv_iternext(ev->vtbl->keymethod);
    if (he) {
	dSP;
	XPUSHs(hv_iterkeysv(he));
	PUTBACK;
    }
}

static void pe_event_FIRSTKEY(void *vptr) {
    pe_event *ev = (pe_event*) vptr;
    hv_iterinit(ev->vtbl->keymethod);
    pe_event_NEXTKEY(ev);
}

static void pe_event_DELETE(void *vptr, SV *svkey)
{
    dTHR;
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
	djSP;
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
  vt->base.Fetch = pe_event_FETCH;
  vt->base.Store = pe_event_STORE;
  vt->base.Firstkey = pe_event_FIRSTKEY;
  vt->base.Nextkey = pe_event_NEXTKEY;
  vt->base.Delete = pe_event_DELETE;
  vt->base.Exists = pe_event_EXISTS;
  vt->new_event = pe_event_allocate;
  vt->dtor = pe_event_dtor;
  vt->stash = gv_stashpv("Event::Event", 1);
  vt->keymethod = newHV();
  hv_store(vt->keymethod, "e_hits", 6, newSViv((IV)_event_hits), 0);
  hv_store(vt->keymethod, "e_prio", 6, newSViv((IV)_event_prio), 0);
  PE_RING_INIT(&vt->freelist, 0);

  vt = &ioevent_vtbl;
  memcpy(vt, &event_vtbl, sizeof(pe_event_vtbl));
  vt->stash = gv_stashpv("Event::Event::Io", 1);
  vt->keymethod = newHVhv(event_vtbl.keymethod);
  hv_store(vt->keymethod, "e_got", 5, newSViv((IV)_event_got), 0);
  vt->new_event = pe_ioevent_allocate;
  vt->dtor = pe_event_dtor;
  PE_RING_INIT(&vt->freelist, 0);

  memset(QueueTime, 0, sizeof(QueueTime));
}
