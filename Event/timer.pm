
package Event::timer;

use Event;
use strict;

register Event;

my @timer = ();

sub new {
    my $self = shift;
    my %arg = @_;
    my $callback = $arg{'-callback'};
    my $when = 0;
    my $interval = 0;
    my $repeat = 0;
    my $obj;

    if(exists $arg{-after}) {
	$interval = $arg{-after};
	$when = time() + $interval;
    }
    elsif(exists $arg{-at}) {
	$when = 0 + $arg{-at};
    }

    if(exists $arg{-interval}) {
	$interval = $arg{-interval};
	$when = time() + $interval
	    unless $when;
	$repeat = 1;
    }

    return undef
	unless $when;

    $obj = bless {
	when      => $when,
	callback  => $callback,
	interval  => $interval,
	repeat    => $repeat,
	queued    => 0,
	cancelled => 0
    }, $self;

    _insert($obj);

    $obj;
}

sub prepare { 
    while(@timer) {
	last unless $timer[0]->{'cancelled'};
	shift @timer;
    }
    @timer
	? $timer[0]->{'when'} - time()
	: 3600;
}

sub check {
    my($name,$timer);
    my @del;
    my $pos = 0;

    while(@timer && ($timer[0]->{'when'} <= time())) {
	my $timer = shift @timer;
	$timer->{'queued'} = 0;
	next if $timer->{'cancelled'};
	Event->queueEvent( 
	    sub {
		$timer->{'callback'}->($timer,$timer->{'when'})
	    }
	);
	$timer->again
	    if $timer->{'repeat'};
    }

    1;
}

sub _insert {
    my $obj = shift;

    return
	if ($obj->{'queued'} || $obj->{'cancelled'});


    my $pos = 0;
    if(@timer) {
	my $time = $obj->{'when'};
	for( ; $pos < length(@timer) ; $pos++) {
	    last if $timer[$pos]->{'when'} >= $time;
	}
    }
    splice(@timer,$pos,0,$obj);
    $obj->{'queued'} = 1;
}

sub again {
    my $timer = shift;

    if($timer->{'interval'} &&
	    !($timer->{'queued'} || $timer->{'cancelled'})) {
	$timer->{'when'} += $timer->{'interval'};
	_insert($timer);
    }
}

sub cancel {
    my $self = shift;
    my $pos;
    for($pos = 0 ; $pos < @timer ; $pos++) {
	if($timer[$pos] == $self) {
	    $self->{'cancelled'} = 0;	# cancel repeating
	    splice(@timer,$pos,1);	# remove it
	    last;
	}
    }
}

1;
