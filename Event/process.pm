
package Event::process;

use Event;
use strict;

registerAsync Event;	# register ourself with Event.pm

my %cb = ();		# hash of pid/callbacks

sub new {
    my $self = shift;
    my %arg = @_;
    my $cb = $arg{'-callback'};

    my $child = exists $arg{'-pid'} ? 0+$arg{'-pid'} : "0";

    my $obj = bless {
	child    => $child,
	callback => $cb,
	canceled => 0
    },$self;

    push(@{$cb{$child} ||= []}, $obj);

    $obj;
}

sub check {
    my @val = _reap();
    while(@val) {
	my($pid,$status) = splice(@val,0,2);

	my $cbq = exists $cb{$pid}
	    ? delete $cb{$pid}
	    : exists $cb{"0"}
		? $cb{"0"}
		: undef;

	if($cbq) {
	    my $cb;
	    foreach $cb (@$cbq) {
		my @a = ($cb, $pid, $status);
		Event->queueAsyncEvent( sub { $cb->{'callback'}->(@a) })
		    unless $cb->{'cancelled'};
	    }
	}
    }
}

sub cancel {
    my $self = shift;
    my $child = $self->{'child'};

    $self->{'canceled'} = 1;

    $cb{$child} = [grep { $_ == $self ? undef : $_ } @{$cb{$child}} ]
	if(defined $cb{$child});
}

1;
