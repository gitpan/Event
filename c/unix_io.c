/*
  I believe this is comparible in efficiency with Apache 1.3.

  Applications with a large number of open fds would benefit by
  an incremental update of the select/poll structures.  Maybe next
  release!

*/

static int pe_sys_fileno(pe_io *ev)
{
  SV *sv = ev->handle;
  IO *io;
  PerlIO *fp;
  
  if (!sv)
    return ev->fd;
  if (SvGMAGICAL(sv))
    mg_get(sv);
  if (SvROK(sv))
    sv = SvRV(sv);
  else if (SvIOK(sv)) /* maybe non-portable but nice for unixen */
    return SvIV(sv);
  if (SvTYPE(sv) == SVt_PVGV) {
    if (!(io=GvIO((GV*)sv)) || !(fp = IoIFP(io))) {
      warn("GLOB(0x%x) isn't a valid IO", sv);
      return -1;
    }
    return PerlIO_fileno(fp);
  }
  
  warn("Can't find fileno");
  sv_dump(ev->handle);
  return -1;
}

/************************************************* POLL */
#if defined(HAS_POLL) && !PE_IO_WAIT
#define PE_IO_WAIT 1

static struct pollfd *Pollfd=0;
static int pollMax=0;
static int Nfds;

static void pe_sys_sleep(double left)
{
  int ret;
  double t0 = EvNOW(1);
  double t1 = t0 + left;
  do {
    ret = poll(0, 0, left * 1000); /* hope zeroes okay */
    if (ret < 0 && errno != EAGAIN && errno != EINTR)
      croak("poll(%.2f) got errno %d", left, errno);
    left = t1 - EvNOW(1);
  } while (left > PE_INTERVAL_EPSILON);
}

static void pe_sys_multiplex(double timeout)
{
  pe_io *ev;
  int xx;
  int ret;
  if (pollMax < IOWatchCount) {
    if (Pollfd)
      safefree(Pollfd);
    pollMax = IOWatchCount+5;
    New(PE_NEWID, Pollfd, pollMax, struct pollfd);
    IOWatch_OK = 0;
  }
  if (!IOWatch_OK) {
    Nfds = 0;
    Zero(Pollfd, pollMax, struct pollfd);
    ev = IOWatch.next->self;
    while (ev) {
      int fd = pe_sys_fileno(ev);
      ev->xref = -1;
      ev->fd = fd;
      if (fd >= 0) {
	int bits=0;
	if (ev->events & PE_IO_R) bits |= (POLLIN | POLLRDNORM);
	if (ev->events & PE_IO_W) bits |= (POLLOUT | POLLWRNORM | POLLWRBAND);
	if (ev->events & PE_IO_E) bits |= (POLLRDBAND | POLLPRI);
	if (bits) {
	  int ok=0;;
	  for (xx = 0; xx < Nfds; xx++) {
	    if (Pollfd[xx].fd == fd) { ok=1; break; }
	  }
	  if (!ok) xx = Nfds++;
	  Pollfd[xx].fd = fd;
	  Pollfd[xx].events |= bits;
	  ev->xref = xx;
	}
      }
      ev = ev->ioring.next->self;
    }
    IOWatch_OK = 1;
  }
  for (xx=0; xx < Nfds; xx++)
    Pollfd[xx].revents = 0; /* needed? XXX */
  if (timeout < 0)
    timeout = 0;
  ret = poll(Pollfd, Nfds, timeout * 1000);
  
  if (ret < 0) {
    if (errno == EINTR || errno == EAGAIN)
      return;
    if (errno == EINVAL) {
      warn("poll: bad args %d %.2f", Nfds, timeout);
      return;
    }
    warn("poll got errno %d", errno);
    return;
  }
  ev = IOWatch.next->self;
  while (ev) {
    int xref = ev->xref;
    if (xref >= 0) {
      int got = 0;
      int mask = Pollfd[xref].revents;
      if (mask & (POLLIN | POLLRDNORM)) got |= PE_IO_R;
      if (mask & (POLLOUT | POLLWRNORM | POLLWRBAND)) got |= PE_IO_W;
      if (mask & (POLLRDBAND | POLLPRI)) got |= PE_IO_E;
      if (got) {
	/* must use | since watcher can trigger more than once
	   before it is serviced */
	ev->got = (ev->got | got) & ev->events;
	if (ev->got) queueEvent((pe_event*) ev, 1);
	/*
	  Can only do this if fd-to-watcher is 1-to-1
	  
	  if (--ret == 0) { ev=0; continue; }
	*/
      }
    }
    ev = ev->ioring.next->self;
  }  
}
#endif /*HAS_POLL*/


/************************************************* SELECT */
#if defined(HAS_SELECT) && !PE_IO_WAIT
#define PE_IO_WAIT 1

static int Nfds;
static fd_set Rfds, Wfds, Efds;

static void pe_sys_sleep(double left)
{
  /* UNTESTED XXX */
  struct timeval tm;
  double t0 = EvNOW(1);
  double t1 = t0 + left;
  int ret;
  do {
    tm.tv_sec = timeout;
    tm.tv_usec = (timeout - tm.tv_sec) * 1000000;
    ret = select(0, 0, 0, 0, &tm);
    if (ret < 0 && errno != EINTR && errno != EAGAIN)
      croak("select(%.2f) got errno %d", left, errno);
    left = t1 - EvNOW(1);
  } while (left > PE_INTERVAL_EPSILON);
}

static void pe_sys_multiplex(double timeout)
{
  struct timeval tm;
  int ret;
  fd_set rfds, wfds, efds;
  pe_io *ev;

  if (!IOWatch_OK) {
    Nfds = -1;
    FD_ZERO(&Rfds);
    FD_ZERO(&Wfds);
    FD_ZERO(&Efds);
    ev = IOWatch.next->self;
    while (ev) {
      int fd = pe_sys_fileno(ev);
      ev->fd = fd;
      if (fd >= 0) {
	int bits=0;
	if (ev->events & PE_IO_R) { FD_SET(fd, &Rfds); ++bits; }
	if (ev->events & PE_IO_W) { FD_SET(fd, &Wfds); ++bits; }
	if (ev->events & PE_IO_E) { FD_SET(fd, &Efds); ++bits; }
	if (bits && fd > Nfds) Nfds = fd;
      }
      ev = ev->ioring.next->self;
    }
    IOWatch_OK = 1;
  }

  if (timeout < 0)
    timeout = 0;
  tm.tv_sec = timeout;
  tm.tv_usec = (timeout - tm.tv_sec) * 1000000;
  if (Nfds > -1) {
    memcpy(&rfds, &Rfds, sizeof(fd_set));
    memcpy(&wfds, &Wfds, sizeof(fd_set));
    memcpy(&efds, &Efds, sizeof(fd_set));
    ret = select(Nfds+1, &rfds, &wfds, &efds, &tm);
  }
  else
    ret = select(0, 0, 0, 0, &tm);

  if (ret < 0) {
    if (errno == EINTR)
      return;
    if (errno == EBADF) {
      ev = IOWatch.next->self;
      while (ev) {
	int fd = ev->fd;
	struct stat buf;
	if (fd >= 0 && PerlLIO_fstat(fd, &buf) < 0 && errno == EBADF) {
	  pe_io_stop((pe_event*) ev);
	  queueEvent((pe_event*) ev, 1);
	  return;
	}
	ev = ev->ioring.next->self;
      }
      warn("select: couldn't find cause of EBADF");
      return;
    }
    if (errno == EINVAL) {
      warn("select: bad args %d %.2f", Nfds, timeout);
      return;
    }
    warn("select got errno %d", errno);
    return;
  }
  ev = IOWatch.next->self;
  while (ev) {
    int fd = ev->fd;
    if (fd >= 0) {
      int got = 0;
      if (FD_ISSET(fd, &rfds)) got |= PE_IO_R;
      if (FD_ISSET(fd, &wfds)) got |= PE_IO_W;
      if (FD_ISSET(fd, &efds)) got |= PE_IO_E;
      if (got) {
	/* must use |= since watcher can trigger more than once
	   before it is serviced */
	ev->got = (ev->got | got) & ev->events;
	if (ev->got) queueEvent((pe_event*) ev, 1);
	/*
	  Can only do this if fd-to-watcher is 1-to-1
	  
	  if (--ret == 0) { ev=0; continue; }
	*/
      }
    }
    ev = ev->ioring.next->self;
  }  
}
#endif /*HAS_SELECT*/



