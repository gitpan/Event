static pe_event *sv_2event(SV *sv)
{
  pe_event *ret = 0;
  if (sv && SvROK(sv)) {
      sv = SvRV(sv);
      assert(sv);
      if (SvOBJECT(sv)) {
	  if (SvTYPE(sv) == SVt_PVHV) {
	      MAGIC *magic = mg_find(sv, '~');
	      SV *ref;
	      assert(magic);
	      ref = magic->mg_obj;
	      assert(ref);
	      if (SvROK(ref) && SvTYPE(SvRV(ref)) == SVt_PVMG) {
		  ret = (pe_event*) SvIV((SV*)SvRV(ref));
	      }
	  }
      }
  }
  if (!ret) {
      sv_dump(sv);
      croak("sv_2event: expected an Event");
  }
  return ret;
}

static SV *event_2sv(pe_event *ev)
{
  SV *tied, *ret;
  HV *stash = ev->stash;
  SV *obj = sv_setref_pv(newSV(0), 0, (void*)ev);
  sv_bless(obj, stash);

  tied = (SV*) newHV();

  sv_magic(tied, obj, '~', Nullch, 0);		/* magic tied, '~', $mgobj */
  --SvREFCNT(obj);				/* make like ref_noinc */

  sv_magic(tied, Nullsv, 'P', 0, 0);

  ret = newRV_noinc(tied);
  sv_bless(ret, stash);

  ++ev->refcnt;

  /*warn("id=%d ++refcnt=%d", ev->id, ev->refcnt); /**/
  return ret;
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
      warn("ignored '%c'", ep[xx]);
    }
    return got;
  }
  else if (SvIOK(sv)) {
    UV extra = SvIV(sv) & ~bits;
    if (extra) warn("ignored extra bits 0x%x", extra);
    return SvIVX(sv) & bits;
  }
  else {
    sv_dump(sv);
    croak("Must be a string /[rwet]/ or bit mask");
  }
}
