use strict;
package Event::process;
use vars qw($DefaultPriority);
$DefaultPriority = Event::Loop::PRIO_HIGH();

'Event'->register();

sub new {
    #lock %Event::;

    shift;
    my %arg = @_;
    my $o = 'Event::process'->allocate();
    Event::init($o, [qw(pid)], \%arg);
    $o->{any} = 1 if !exists $o->{pid};
    $o->start();
    $o;
}

my %cb;		# pid => [events]

Event->signal(signal => 'CHLD',  #CLD? XXX
	      callback => sub {
		  my ($o) = @_;
		  for (my $x=0; $x < $o->{count}; $x++) {
		      my $pid = wait;
		      last if $pid == -1;
		      my $status = $?;
		      
		      my $cbq = delete $cb{$pid} if exists $cb{$pid};
		      $cbq ||= $cb{any} if exists $cb{any};
		      
		      next if !$cbq;
		      for my $e (@$cbq) {
			  $e->{pid} = $pid;
			  $e->{status} = $status;
			  Event::Loop::queueEvent($e)
		      }
		  }
	      },
	      desc => "Event::process");

sub start {
    my ($o, $repeat) = @_;
    my $key = exists $o->{any}? 'any' : $o->{pid};
    push @{$cb{ $key } ||= []}, $o;
}

sub stop {
    my $o = shift;
    my $key = exists $o->{any}? 'any' : $o->{pid};
    $cb{ $key } = [grep { $_ != $o } @{$cb{ $key }} ];
    delete $cb{ $key } if
	@{ $cb{ $key }} == 0;
}

1;
__END__


MODULE = Event		PACKAGE = Event::process


#ifdef I_SYS_WAIT
#  include <sys/wait.h>
#endif

#define MAX_CHLD_SLOT 63
#define CHLD_BUF_SIZE (MAX_CHLD_SLOT+1)

struct child_data {
    int pid;
    int status;
};

static VOL int chld_next;
static struct child_data chld_buf[CHLD_BUF_SIZE];

static Signal_t
chld_sighandler(sig)
int sig;
{
    int pid;
    int status;
    int slot = chld_next++;

    if(slot >= MAX_CHLD_SLOT)
	(void)rsignal(SIGCHLD, SIG_DFL);

#ifdef Wait4Any
    pid = Wait4Any(&status);
#else
    pid = wait(&status);
#endif
    chld_buf[slot].pid = pid;
    chld_buf[slot].status = status;

    if(chld_next < CHLD_BUF_SIZE)
	(void)rsignal(SIGCHLD, chld_sighandler);
}

#ifdef WNOHANG
# ifdef HAS_WAITPID
#  define Wait4Any(s) waitpid(-1,(s),WNOHANG)
# else
#  ifdef HAS_WAIT4
#   define Wait4Any(s) wait4(-1,(s),WNOHANG,0)
#  endif
# endif
#endif

static void
boot_process()
{
  chld_next=0;
  memzero(chld_buf, sizeof(chld_buf));
  (void)rsignal(SIGCHLD, chld_sighandler);
}

int
_count(...)
PROTOTYPE:
CODE:
    RETVAL = chld_next;
OUTPUT:
    RETVAL

void
_reap()
PROTOTYPE:
PPCODE:
{
    int count = 0;
    if(chld_next) {
	int pid, status, slot;
	(void)rsignal(SIGCHLD, SIG_DFL);
	slot = chld_next;
	EXTEND(sp, (slot * 2));
	while( slot-- ) {
	    if(chld_buf[slot].pid > 0) {
		count += 2;
		XPUSHs(sv_2mortal(newSViv(chld_buf[slot].pid)));
		XPUSHs(sv_2mortal(newSViv(chld_buf[slot].status)));
	    }
	}
	chld_next = 0;
#ifdef Wait4Any
	while((pid = Wait4Any(&status)) > 0) {
	    EXTEND(sp,2);
	    count += 2;
	    XPUSHs(sv_2mortal(newSViv(pid)));
	    XPUSHs(sv_2mortal(newSViv(status)));
	}
#endif
	(void)rsignal(SIGCHLD, chld_sighandler);
    }
    XSRETURN(count);
}

