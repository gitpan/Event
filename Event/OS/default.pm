use strict;
package Event::OS;
use Carp;
use IO::Poll '/POLL/';
use Time::HiRes qw(sleep);

my $poll = 'IO::Poll'->new();

sub WaitForEvent {
    my $timeout = shift;
    $timeout = 0 if $timeout < 0;

    if ($poll->handles) {
	$poll->poll($timeout);
    }
    else {
	sleep $timeout;
    }
}

# poll is probably more complicated than necessary
# for almost all applications

sub calc_mask {
    my $events = shift;
    return $events if $events =~ /^\d+$/;
    my $mask = 0;
    for (0 .. length($events) - 1) {
	my $c = substr($events, $_, 1);
	if ($c eq 'r') {
	    $mask |= POLLIN | POLLRDNORM;
	} elsif ($c eq 'w') {
	    $mask |= POLLOUT | POLLWRNORM;
	} elsif ($c eq 'e') {
	    $mask |= POLLRDBAND | POLLPRI | POLLHUP;
	} else {
	    carp "unrecognized event specification '$c' ignored";
	}
    }
    $mask;
}

sub calc_events {
    my $mask = shift;
    my $ev = '';
    if ($mask & (POLLIN | POLLRDNORM)) {
	$ev .= 'r';
    }
    if ($mask & (POLLOUT | POLLWRNORM)) {
	$ev .= 'w';
    }
    if ($mask & (POLLRDBAND | POLLPRI | POLLHUP)) {
	$ev .= 'e';
    }
    $ev;
}

sub AddSource {
    my($obj,$event) = @_;

    if (ref($obj) && UNIVERSAL::isa($obj,'IO::Handle')) {
	my $mask = $poll->mask($obj) || 0;
	my $new = calc_mask($event);

	croak("More than one event is waiting for ''".
	      calc_events($mask & $new)."' on $obj")
	    if $mask & $new;

	$poll->mask($obj, $mask | $new);
    } else {
	croak "Unknown event source object '$obj'";
    }
}

sub RemoveSource {
    my ($obj, $event) = @_;
    $poll->mask($obj, $poll->mask($obj) & calc_mask($event));
}

sub GotEvent {

    # OPTIMIZE!!
    #
    # Can we stash the file descriptor and event mask in the
    # event object somewhere?

    my ($obj,$events) = @_;
    $poll->events($obj) & calc_mask($events)
}

1;

__END__


FROM: gbarr@pobox.com

On Fri, Jul 17, 1998 at 02:35:09PM -0400, Joshua Pritikin wrote:
> At one time you mentioned that you prefer the select API vs.
> the poll API.  I am curious as to your preference.  It seems
> API emulation is equally annoying in either direction?

select probably does all that is needed and poll emulation is a pain.
emulating select with poll is *much* easier.

