static pe_timer *RollTimer;

static void pe_stat_init(pe_stat *st)
{
  int xx;
  st->xsec = 0;
  for (xx=0; xx < PE_STAT_I1; xx++) {
    st->sec[xx].elapse = 0;
    st->sec[xx].ran = 0;
  }
  st->xmin = 0;
  for (xx=0; xx < PE_STAT_I2; xx++) {
    st->min[xx].elapse = 0;
    st->min[xx].ran = 0;
  }
}

#if 0
static void pe_stat_dump(pe_stat *st)
{
  int xx;
  fprintf(stderr,"stat(0x%x)\nsec> ", st);
  for (xx=0; xx < PE_STAT_I1; xx++) {
    pe_run *run = &st->sec[(st->xsec + xx) % PE_STAT_I1];
    fprintf(stderr, "%5.2f/%d ", run->elapse, run->ran);
  }
  fprintf(stderr,"\nmin5> ");
  for (xx=0; xx < PE_STAT_I2; xx++) {
    pe_run *run = &st->min[(st->xmin + xx) % PE_STAT_I2];
    fprintf(stderr, "%5.2f/%d ", run->elapse, run->ran);
  }
  fprintf(stderr,"\n");
}
#endif

static void pe_stat_record(pe_stat *st, double elapse)
{
  pe_run *run = &st->sec[st->xsec];
  /*  warn("recording %f\n", elapse);  pe_stat_dump(st); /**/
  run->elapse += elapse;
  run->ran += 1;
  /*  pe_stat_dump(st); /**/
}

static void pe_stat_query(pe_stat *st, int sec, int *ran, double *elapse)
{
  *ran = 0;
  *elapse = 0;
  if (sec <= 1)
    return;
  if (sec <= PE_STAT_SECONDS * PE_STAT_I1) {
    int xx;
    for (xx=0; xx <= (sec-1) / PE_STAT_SECONDS; xx++) {
      pe_run *run = &st->sec[(st->xsec + xx + 1) % PE_STAT_I1];
      assert(xx <= PE_STAT_I1);
      *ran += run->ran;
      *elapse += run->elapse;
    }
    return;
  }
  if (sec <= PE_STAT_SECONDS * PE_STAT_I1 * PE_STAT_I2) {
    int xx;
    for (xx=0; xx <= (sec-1) / (PE_STAT_SECONDS*PE_STAT_I1); xx++) {
      pe_run *run = &st->min[(st->xmin + xx + 1) % PE_STAT_I2];
      assert(xx <= PE_STAT_I2);
      *ran += run->ran;
      *elapse += run->elapse;
    }
    return;
  }
  warn("Stats available only for the last %d seconds (vs. %d)",
       PE_STAT_SECONDS * PE_STAT_I1 * PE_STAT_I2, sec);
}

static void pe_stat_roll(pe_stat *st)
{
  st->xsec = (st->xsec + PE_STAT_I1 - 1) % PE_STAT_I1;
  if (st->xsec == 0) {
    int xx;
    st->xmin = (st->xmin + PE_STAT_I2 - 1) % PE_STAT_I2;
    st->min[st->xmin].ran = 0;
    st->min[st->xmin].elapse = 0;
    for (xx=0; xx < PE_STAT_I1; xx++) {
      st->min[st->xmin].ran += st->sec[xx].ran;
      st->min[st->xmin].elapse += st->sec[xx].elapse;
    }
  }
  st->sec[st->xsec].ran = 0;
  st->sec[st->xsec].elapse = 0;
}

static void pe_stat_roll_cb()
{
  pe_event *ev = AllEvents.next->self;
  while (ev) {
    pe_stat_roll(&ev->stats);
    ev = ev->all.next->self;
  }
  pe_stat_roll(&idleStats);
}

static void pe_stat_restart()
{
  if (!Stats) {
    pe_event *ev = AllEvents.next->self;
    /*    warn("reinit stats"); /**/
    while (ev) {
      pe_stat_init(&ev->stats);
      ev = ev->all.next->self;
    }
    pe_stat_init(&idleStats);
    
    cacheNow();
    RollTimer = (pe_timer*) pe_timer_allocate();
    RollTimer->at = Now;
    RollTimer->interval = PE_STAT_SECONDS;
    ev = (pe_event*) RollTimer;
    EvREPEAT_on(ev);
    sv_setpv(ev->desc, "Event::Stats");
    ev->c_callback = pe_stat_roll_cb;
    pe_timer_start(ev, 0);
  }
  ++Stats;
}

static void pe_stat_stop()
{
  pe_event *ev;
  if (Stats) {
    if (--Stats)
      return;

    ev = (pe_event*) RollTimer;
    ev->c_callback = 0;
    pe_timer_stop(ev);
  }
}

static void boot_stats()
{
  HV *stash = gv_stashpv("Event::Stats", 1);
  newCONSTSUB(stash, "MAXTIME",
	      newSViv(PE_STAT_SECONDS * PE_STAT_I1 * PE_STAT_I2));
}
