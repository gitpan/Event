static struct pe_watcher_vtbl pe_timer_vtbl;

static pe_watcher *pe_timer_allocate(HV *stash, SV *temple) {
    pe_timer *ev;
    EvNew(7, ev, 1, pe_timer);
    assert(ev);
    ev->base.vtbl = &pe_timer_vtbl;
    PE_RING_INIT(&ev->tm.ring, ev);
    ev->tm.at = 0;
    ev->interval = &PL_sv_undef;
    pe_watcher_init(&ev->base, stash, temple);
    return (pe_watcher*) ev;
}

static void pe_timer_dtor(pe_watcher *ev) {
    pe_timer *tm = (pe_timer*) ev;
    SvREFCNT_dec(tm->interval);
    pe_watcher_dtor(ev);
}

static void pe_timer_start(pe_watcher *ev, int repeat) {
    pe_timer *tm = (pe_timer*) ev;
    if (repeat) {
	/* We just finished the callback and need to re-insert at
	   the appropriate time increment. */
	double interval;

	if (!sv_2interval(tm->interval, &interval))
	    croak("Repeating timer with no interval");
	if (interval <= 0)
	    croak("Timer has non-positive interval");

	tm->tm.at = interval + (WaHARD(ev)? tm->tm.at : NVtime());
    }
    if (!tm->tm.at)
	croak("Timer unset");

    pe_timeable_start(&tm->tm);
}

static void pe_timer_stop(pe_watcher *ev)
{ pe_timeable_stop(&((pe_timer*)ev)->tm); }

static void pe_timer_alarm(pe_watcher *wa, pe_timeable *tm) {
    pe_event *ev = (*wa->vtbl->new_event)(wa);
    ++ev->hits;
    queueEvent(ev);
}

WKEYMETH(_timer_at) {
    pe_timer *tp = (pe_timer*)ev;
    if (!nval) {
	dSP;
	XPUSHs(sv_2mortal(newSVnv(tp->tm.at)));
	PUTBACK;
    } else {
	int active = WaPOLLING(ev);
	if (active) pe_watcher_off(ev);
	tp->tm.at = SvNV(nval);
	if (active) pe_watcher_on(ev, 0);
    }
}

WKEYMETH(_timer_interval) {
    pe_timer *tp = (pe_timer*)ev;
    if (!nval) {
	dSP;
	XPUSHs(tp->interval);
	PUTBACK;
    } else {
	SV *old = tp->interval;
	tp->interval = SvREFCNT_inc(nval);
	SvREFCNT_dec(old);
    }
}

static void boot_timer() {
    pe_watcher_vtbl *vt = &pe_timer_vtbl;
    memcpy(vt, &pe_watcher_base_vtbl, sizeof(pe_watcher_base_vtbl));
    vt->dtor = pe_timer_dtor;
    vt->start = pe_timer_start;
    vt->stop = pe_timer_stop;
    vt->alarm = pe_timer_alarm;
    pe_register_vtbl(vt, gv_stashpv("Event::timer",1), &event_vtbl);
}
