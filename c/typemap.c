static void invalidate_sv(SV *ref)
{
  SV *iobj;
  assert(ref);
  assert(SvROK(ref));
  iobj = SvRV(ref);
  assert(SvTYPE(iobj) == SVt_PVHV);
  assert(SvOBJECT(iobj));
  HvNAME(iobj) = 0;
  /*warn("Invalidate (%s=0x%x)", HvNAME(SvSTASH(iobj)), iobj); /**/
}

static void unwrap_obj(SV *sv, void **vpp, int *is_event)
{
    SV *origsv = sv;
    assert(vpp);
    *vpp=0;
    if (sv && SvROK(sv)) {
	sv = SvRV(sv);
	assert(sv);
	if (SvOBJECT(sv) && SvTYPE(sv) == SVt_PVHV) {
	    /* also can try xhv_array? */
	    *vpp = HvNAME(sv);
	}
    } else {
	croak("Not a reference?");
    }
    if (!*vpp)
	croak("Attempt to use destroyed object (RV=0x%x %s=0x%x)",
	      origsv, HvNAME(SvSTASH(sv)), sv);
    if (is_event) *is_event = ((pe_watcher*)*vpp)->vtbl->base.is_event;
}

static void decode_sv(SV *sv, pe_watcher **wap, pe_event **evp)
{
  void *vp;
  int is_event;
  unwrap_obj(sv, &vp, &is_event);
  assert(vp);
  if (!is_event) {
    assert(wap);
    *wap = (pe_watcher *) vp;
    if (evp) *evp=0;
  }
  else {
    pe_event *ev = (pe_event*) vp;
    assert(wap || evp);
    if (evp) *evp = ev;
    if (wap) *wap = ev->up;
  }
}

static SV *wrap_tiehash(void *ptr, HV *stash)
{
  SV *tied = (SV*) newHV();
  HvNAME(tied) = ptr;
  /* HvARRAY(tied) = 0x12341234; /**/
  sv_magic(tied, Nullsv, 'P', 0, 0);
  return sv_bless(newRV_noinc(tied), stash);
}

static SV *watcher_2sv(pe_watcher *wa) {
    if (!wa->mysv) {
	wa->mysv = wrap_tiehash(wa, wa->stash);
    }
    return wa->mysv;
}

static SV *event_2sv(pe_event *ev) {
    if (!ev->mysv) {
	STRLEN n_a;
	ev->mysv = wrap_tiehash(ev, ev->up->stash);
	if (EvDEBUGx(ev) >= 4)
	    warn("Event=0x%x '%s' wrapped with SV=0x%x",
		 ev, SvPV(ev->up->desc, n_a), SvRV(ev->mysv)); /**/
    }
    return ev->mysv;
}

static void get_base_vtbl(SV *sv, void **vp, pe_base_vtbl **vt) {
  assert(vp && vt);
  unwrap_obj(sv, vp, 0);
  *vt = **(pe_base_vtbl***)vp;
}

/***************************************************************/

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

static int sv_2events_mask(SV *sv, int bits)
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
    /* nuke? XXX */
}
