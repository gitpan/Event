use strict;
package Event::inactivity;
use Carp;
use Event qw(PRIO_NORMAL time queue);
use base 'Event::Watcher::Tied';
use vars qw($DefaultPriority);
$DefaultPriority = PRIO_NORMAL + 1;

warn "Event::inactivity is deprecated";

'Event::Watcher'->register();

sub new {
    my $class = shift;
    my %arg = @_;
    my $o = $class->allocate(delete $arg{attach_to} || {});

    # deprecated
    for (qw(timeout level)) {
	if (exists $arg{"e_$_"}) {
	    carp "'e_$_' is renamed to '$_'";
	    $arg{$_} = delete $arg{"e_$_"};
	}
    }

    $o->repeat(1);
    $o->{e_timeout} = delete $o->{timeout} || 10;
    $o->{e_level} = delete $o->{level} || PRIO_NORMAL;
    $o->init(\%arg);
    $o;
}

sub timeout {
    my $o = shift;
    if (! @_) {
	$o->{e_timeout};
    } else {
	$o->{e_timeout} = shift;
    }
}

sub level {
    my $o=shift;
    if (!@_) {
	$o->{e_level}
    } else {
	$o->{e_level} = shift;
    }
}

sub _start {
    my ($o, $repeating) = @_;
    $o->{e_prev_test} = time;
    $o->at(time + $o->{e_timeout});
}

sub _alarm {
    my ($o) = @_;
    my $qt = Event::queue_time($o->{e_level});
    if ($qt and $qt > $o->{e_prev_test}) {
	$o->{e_prev_test} = $qt;
    }
    my $left = $o->{e_prev_test} + $o->{e_timeout} - time;
    if ($left > 0.0002) {  #EPSILON XXX
	$o->at(time + $left);
    } else {
	queue($o);
    }
}

1;
