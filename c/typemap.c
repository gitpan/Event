
static pe_event *
sv_2event(SV *sv)
{
  pe_event *ret = 0;
  if (sv && SvROK(sv)) {
      sv = SvRV(sv);
      assert(sv);
      if (SvOBJECT(sv)) {

	  /* We need two cases here because normal method calls cannot
	     be the same as 'tie' method calls.  This will change! */

	  if (SvTYPE(sv) == SVt_PVHV) {
	      MAGIC *magic = mg_find(sv, 'P');
	      SV *ref;
	      assert(magic);
	      ref = magic->mg_obj;
	      assert(ref);
	      if (SvROK(ref) && SvTYPE(SvRV(ref)) == SVt_PVMG) {
		  ret = (pe_event*) SvIV((SV*)SvRV(ref));
	      }

	  } else if (SvTYPE(sv) == SVt_PVMG) {
	      ret = (pe_event*) SvIV(sv);
	  }
      }
  }
  if (!ret) {
      sv_dump(sv);
      croak("sv_2event: expected an Event");
  }
  return ret;
}

static SV *
event_2sv(pe_event *ev)
{
  SV *tied, *ret;
  HV *stash = ev->stash;
  SV *obj = sv_setref_pv(newSV(0), 0, (void*)ev);
  sv_bless(obj, stash);
  tied = (SV*) newHV();
  sv_magic(tied, obj, 'P', 0, 0);
  ret = newRV_noinc(tied);
  sv_bless(ret, stash);
  ++ev->refcnt;
  --SvREFCNT(obj);
  /*warn("id=%d ++refcnt=%d", ev->id, ev->refcnt); /**/
  return ret;
}
