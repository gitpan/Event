use strict;
package Event::signal;
BEGIN { 'Event::Loop'->import(qw(PRIO_HIGH queueEvent)); }

'Event'->registerAsync;

my %sig;

BEGIN {
    # populate %sig

    my @sig = grep { /^[A-Z]/ } map { RealSigName($_) } keys %SIG;
    @sig{@sig} = (undef) x @sig;

    # delete un-trapable signals

    delete $sig{KILL};
    delete $sig{STOP};
    delete $sig{ZERO};

    # and delete SIGCHLD as it is handled by Event::Process

    my $chld = RealSigName('CHLD') || RealSigName('CLD');
    delete $sig{$chld} if defined $chld;
}

sub new {
    # lock %Event::

    shift;
    my %arg = @_;
    for (qw(signal callback)) {
	$arg{$_} = $arg{"-$_"} if exists $arg{"-$_"};
    }

    my $name = $arg{'signal'};

    # only accept callbacks for signals in %sig
    return unless exists $sig{$name};

    $arg{priority} = PRIO_HIGH + ($arg{priority} or 0);

    my $obj = bless \%arg, __PACKAGE__;

    push @{$sig{$name} ||= []}, $obj;

    _watch_signal($name);

    Event::init($obj);
}

sub cancel {
    # lock %Event::

    my $o = shift;
    my $name = $o->{'signal'};

    $sig{$name} = [ grep { $_ == $o ? undef : $_ } @{$sig{$name}} ];
    _unwatch_signal($name) if @{$sig{$name}} == 0;

    $o->SUPER::cancel();
}

sub check {
    my @val = _reap();

    while(@val) {
	my($name,$count) = splice(@val,0,2);
	next if !exists $sig{$name}; #?

	for my $e (@{$sig{$name}}) {
	    
	    my $cb = $e->{'callback'};
	    my $sub;
	    if (!$Event::DebugLevel) {
		$sub = sub {
		    $cb->($e, $name, $count);
		};
	    } else {
		$sub = sub {
		    Event::invoking($e);
		    $cb->($e, $name, $count);
		    Event::completed($e);
		};
	    }
	    queueEvent($e->{priority}, $sub);
	}
    }
}

1;
