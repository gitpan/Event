use strict;
package Event::timer;
use Carp;
use Time::HiRes qw(time);
BEGIN { 'Event::Loop'->import(qw(PRIO_NORMAL queueEvent)); }

'Event'->register;

my @timer = ();

sub new {
#    lock %Event::;

    shift;
    my %arg = @_;

    for (qw(desc repeat after at interval hard callback)) {
	$arg{$_} = $arg{"-$_"} if exists $arg{"-$_"};
    }

    if (exists $arg{after}) {
	croak "'after' and 'at' are mutually exclusive"
	    if exists $arg{at};
	$arg{when} = time() + $arg{after};
	$arg{interval} = $arg{after} if !exists $arg{interval};
    }
    elsif (exists $arg{at}) {
	$arg{when} = 0 + $arg{at};
    }

    if (exists $arg{interval}) {
	$arg{when} = time() + $arg{interval}
	    unless $arg{when};
	$arg{repeat} = 1;
    }

    croak "Event->timer with incomplete specification"
	unless $arg{when};

    $arg{priority} = PRIO_NORMAL + ($arg{priority} or 0);
    $arg{hard} ||= 0;

    my $o = Event::init(bless \%arg, __PACKAGE__);
    _insert($o);
    $o;
}

sub prepare { 
    @timer? $timer[0]->{'when'} - time() : 3600;
}

sub check {
    my $now = time();
    while (@timer and $timer[0]->{'when'} <= $now) {
	my $timer = shift @timer;
	$timer->{queued} = 0;
	next if $timer->{'cancelled'};

	my $cb = $timer->{'callback'};
	my $sub;
	if (!$Event::DebugLevel) {
	    $sub = sub {
		$cb->($timer,$timer->{'when'});
	    };
	} else {
	    $sub = sub {
		Event::invoking($timer);
		$cb->($timer,$timer->{'when'});
		Event::completed($timer);
	    };
	}
	queueEvent($timer->{'priority'}, $sub);
	$timer->again if $timer->{'repeat'};
    }
}

sub _insert {
    my $obj=shift;

    # Should do binary insertion sort?  No.  Long-term timers deserve
    # the performance hit.  Code should be optimal for short-term timers.

    my $pos=0;
    if (@timer) {
	my $when = $obj->{'when'};
	for (; $pos < @timer; $pos++) {
	    last if $when < $timer[$pos]->{'when'};
	}
    }
    splice @timer,$pos,0,$obj;

    # sloppy, but correct
#    @timer = sort { $$a{when} <=> $$b{when} } @timer, $obj;

    if ($Event::DebugLevel >= 3) {
	my @sorted = sort { $$a{when} <=> $$b{when} } @timer;
	for (my $x=0; $x < @sorted; $x++) {
	    if ($sorted[$x] != $timer[$x]) {
		warn "timers out of order!\n";
		for (my $z=0; $z < @sorted; $z++) {
		    my ($s,$u) = ($sorted[$z], $timer[$z]);
		    warn "$$s{when} -- $$u{when}\n";
		}
		last;
	    }
	}
    }
}

sub again {
    my $obj = shift;

    croak "$obj->again: is cancelled"
	if $obj->{'cancelled'};
    croak "$obj->again: no interval specified"
	unless exists $obj->{'interval'};

    return if $obj->{queued};
    $obj->{queued} = 1;

    $obj->{'when'} = $obj->{'interval'} +
	($obj->{'hard'} ? $obj->{'when'} : time());
    _insert($obj);
}

1;
