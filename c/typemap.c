static HV *WatcherInnerStash;
static HV *EventStash;

static void invalidate_sv(SV *ref)
{
  SV *iobj;
  assert(ref);
  assert(SvROK(ref));
  iobj = SvRV(ref);
  assert(SvTYPE(iobj) == SVt_PVMG);
  assert(SvOBJECT(iobj));
  sv_setiv(iobj, 0);
  /* croak("Couldn't invalidate SV=%x", sv); /**/
}

static SV *unwrap_self_tied_hash(SV *sv)
{
    if (sv && SvROK(sv)) {
	sv = SvRV(sv);
	assert(sv);
	if (SvOBJECT(sv)) {
	    if (SvTYPE(sv) == SVt_PVHV) {
		MAGIC *magic = mg_find(sv, '~');
		SV *ref, *iobj;
		assert(magic);
		ref = magic->mg_obj;
		assert(ref);
		assert(SvROK(ref));
		iobj = SvRV(ref);
		if (SvTYPE(iobj) == SVt_PVMG) {
		    assert(SvOBJECT(iobj));
		    assert(SvIOK(iobj));
		    return iobj;
		}
	    }
	}
    }
    return 0;
}

static void unwrap_obj(SV *sv, void **vpp, HV **stpp)
{
    SV *iobj = unwrap_self_tied_hash(sv);
    if (!iobj) {
	sv_dump(sv);
	croak("unwrap_obj");
    }
    assert(vpp);
    *vpp = (void*) SvIVX(iobj);
    if (!*vpp)
	croak("Attempt to use destroyed object (%s=0x%x)",
	      HvNAME(SvSTASH(iobj)), iobj);
    if (stpp) *stpp = SvSTASH(iobj);
}

static void decode_sv(SV *sv, pe_watcher **wap, pe_event **evp)
{
  void *vp;
  HV *stash;
  unwrap_obj(sv, &vp, &stash);
  assert(vp);
  if (stash == WatcherInnerStash) {
    assert(wap);
    *wap = (pe_watcher *) vp;
    if (evp) *evp=0;
  }
  else if (stash == EventStash) {
    pe_event *ev = (pe_event*) vp;
    assert(wap || evp);
    if (evp) *evp = ev;
    if (wap) *wap = ev->up;
  } else {
    sv_dump(sv);
    croak("decode_sv");
  }
}

static void get_base_vtbl(SV *sv, void **vp, pe_base_vtbl **vt)
{
  assert(vp && vt);
  unwrap_obj(sv, vp, 0);
  *vt = **(pe_base_vtbl***)vp;
}

static SV *make_inner_sv(void *ptr, HV *inner_stash)
{
  SV *obj = sv_setref_pv(newSV(0), 0, ptr);
  assert(inner_stash);
  sv_bless(obj, inner_stash);
  --obj->sv_refcnt;
  return obj;
}

static SV *wrap_tiehash(SV *inner_sv, HV *stash)
{
  SV *tied = (SV*) newHV();
  sv_magic(tied, inner_sv, '~', Nullch, 0);	/* magic tied, '~', $mgobj */
  sv_magic(tied, Nullsv, 'P', 0, 0);
  return sv_bless(newRV_noinc(tied), stash);
}

static SV *watcher_2sv(pe_watcher *wa)
{
  if (!wa->mysv) {
    wa->mysv = make_inner_sv(wa, WatcherInnerStash);
    /* warn("%x mysv=%x", wa, wa->mysv); /**/
  }
  return wrap_tiehash(wa->mysv, wa->stash);
}

static SV *event_2sv(pe_event *ev)
{
    if (!ev->mysv) {
	ev->mysv = make_inner_sv(ev, EventStash);
    }
    return wrap_tiehash(ev->mysv, ev->up->stash);
}

static int sv_2interval(SV *in, double *out)
{
  SV *sv = in;
  if (!sv) return 0;
  if (SvGMAGICAL(sv))
    mg_get(sv);
  if (!SvOK(sv)) return 0;
  if (SvROK(sv))
    sv = SvRV(sv);
  if (SvNOK(sv)) {
    *out = SvNVX(sv);
    return 1;
  }
  if (SvIOK(sv)) {
    *out = SvIVX(sv);
    return 1;
  }
  if (looks_like_number(sv)) {
    *out = SvNV(sv);
    return 1;
  }
  sv_dump(in);
  croak("Interval must be a number of reference to a number");
  return 0;
}

static SV* events_mask_2sv(int mask)
{
  STRLEN len;
  SV *ret = newSV(0);
  SvUPGRADE(ret, SVt_PVIV);
  sv_setpvn(ret, "", 0);
  if (mask & PE_R) sv_catpv(ret, "r");
  if (mask & PE_W) sv_catpv(ret, "w");
  if (mask & PE_E) sv_catpv(ret, "e");
  if (mask & PE_T) sv_catpv(ret, "t");
  SvIVX(ret) = mask;
  SvIOK_on(ret);
  return ret;
}

static int sv_2events_mask(SV *sv, int bits) /*poll mask XXX*/
{
  if (SvPOK(sv)) {
    UV got=0;
    int xx;
    STRLEN el;
    char *ep = SvPV(sv,el);
    for (xx=0; xx < el; xx++) {
      switch (ep[xx]) {
      case 'r': if (bits & PE_R) { got |= PE_R; continue; }
      case 'w': if (bits & PE_W) { got |= PE_W; continue; }
      case 'e': if (bits & PE_E) { got |= PE_E; continue; }
      case 't': if (bits & PE_T) { got |= PE_T; continue; }
      }
      warn("Ignored '%c' in poll mask", ep[xx]);
    }
    return got;
  }
  else if (SvIOK(sv)) {
    UV extra = SvIVX(sv) & ~bits;
    if (extra) warn("Ignored extra bits (0x%x) in poll mask", extra);
    return SvIVX(sv) & bits;
  }
  else {
    sv_dump(sv);
    croak("Must be a string /[rwet]/ or bit mask");
    return 0; /* NOTREACHED */
  }
}

static void boot_typemap()
{
  EventStash = gv_stashpv("Event::Event", 1);
  SvREFCNT_inc(EventStash);
  WatcherInnerStash = gv_stashpv("Event::Watcher::Inner", 1);
  SvREFCNT_inc(WatcherInnerStash);
}
