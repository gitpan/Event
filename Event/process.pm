use strict;
package Event::process;
BEGIN { 'Event::Loop'->import(qw(PRIO_HIGH queueEvent)); }

'Event'->registerAsync;

my %cb = ();		# hash of pid/callbacks

sub new {
    #lock %Event::;

    shift;
    my %arg = @_;
    for (qw(callback pid)) {
	$arg{$_} = $arg{"-$_"} if exists $arg{"-$_"};
    }

    $arg{priority} = PRIO_HIGH + ($arg{priority} or 0);
    $arg{pid} = exists $arg{'pid'} ? 0+$arg{'pid'} : "0";

    my $obj = bless \%arg, __PACKAGE__;

    push @{$cb{ $obj->{pid} } ||= []}, $obj;

    Event::init($obj);
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

	if ($cbq) {
	    for my $e (@$cbq) {
		my @a = ($e, $pid, $status);
		my $cb = $e->{'callback'};
		my $sub;
		if (!$Event::DebugLevel) {
		    $sub = sub { $cb->(@a); };
		} else {
		    $sub = sub {
			Event::invoking($e);
			$cb->(@a);
			Event::completed($e);
		    };
		}
		queueEvent($e->{priority}, $sub);
	    }
	}
    }
}

sub cancel {
    my $self = shift;
    my $child = $self->{'pid'};

    $cb{$child} = [grep { $_ == $self ? undef : $_ } @{$cb{$child}} ];

    $self->SUPER::cancel();
}

1;
