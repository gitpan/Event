static SV *wrap_watcher(void *ptr, HV *stash, SV *temple) {
    SV *ref;
    MAGIC **mgp;
    MAGIC *mg;

    assert(ptr);
    assert(stash);

    if (!temple)
	temple = (SV*)newHV();
    else
	SvREFCNT_inc(temple);
    if (SvOBJECT(temple))
	croak("Can't attach to blessed reference");
    assert(!SvROK(temple));
    assert(mg_find(temple, '~') == 0); /* multiplicity disallowed! */

    ref = newRV_noinc(temple);
    sv_bless(ref, stash);

    mgp = &SvMAGIC(temple);
    while ((mg = *mgp))
	mgp = &mg->mg_moremagic;

    New(0, mg, 1, MAGIC);
    Zero(mg, 1, MAGIC);
    mg->mg_type = '~';
    mg->mg_obj = (SV*) ptr;  /* NOT refcnt'd */
    *mgp = mg;

    return ref;
}

static SV *watcher_2sv(pe_watcher *wa) { /**SLOW IS OKAY**/
    assert(!WaDESTROYED(wa));
    if (!wa->mysv) {
	wa->mysv = wrap_watcher(wa, wa->vtbl->stash, 0);
	if (WaDEBUGx(wa) >= 4) {
	    STRLEN n_a;
	    warn("Watcher=0x%x '%s' wrapped with SV=0x%x",
		 wa, SvPV(wa->desc, n_a), SvRV(wa->mysv));
	}
    }
    return SvREFCNT_inc(sv_2mortal(wa->mysv));
}

static void* sv_2watcher(SV *sv) {
    MAGIC *mg;
    SV *origsv = sv;
    if (!sv || !SvROK(sv))
	croak("sv_2watcher: not a reference?");
    sv = SvRV(sv);
    mg = mg_find(sv, '~');
    if (mg) {
	return (void*) mg->mg_obj;
    }
    croak("Attempt to use destroyed object (RV=0x%x %s=0x%x)",
	  origsv, HvNAME(SvSTASH(sv)), sv);
    return 0;
}

/*
  Events have a short lifetime.  mysv is kept alive until the event
  has been serviced.  Once perl finally releases mysv then the event
  is deallocated (or, more likely, recycled).
*/

static SV *event_2sv(pe_event *ev) { /**MAKE FAST**/
    if (!ev->mysv) {
	SV *rv = newSV(0);
	SV *sv = newSVrv(rv,0);
	sv_bless(rv, ev->vtbl->stash);
	sv_setiv(sv, (IV)ev);
	ev->mysv = rv;

	if (WaDEBUGx(ev->up) >= 4) {
	    STRLEN n_a;
	    warn("Event=0x%x '%s' wrapped with SV=0x%x",
		 ev, SvPV(ev->up->desc, n_a), SvRV(ev->mysv));
	}
    }
    return SvREFCNT_inc(sv_2mortal(ev->mysv));
}

static void *sv_2event(SV *sv) {
    void *ptr;
    assert(sv);
    assert(SvROK(sv));
    sv = SvRV(sv);
    ptr = (void*) SvIV(sv);
    assert(ptr);
    return ptr;
}

/***************************************************************/

static int sv_2interval(SV *in, double *out) {
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

static SV* events_mask_2sv(int mask) {
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

static int sv_2events_mask(SV *sv, int bits) {
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
