use strict;
package Event::inactivity;
use Carp;
use Event qw(PRIO_NORMAL time queue);
use base 'Event::Watcher';
use vars qw($DefaultPriority);
$DefaultPriority = PRIO_NORMAL + 1;

'Event::Watcher'->register();

sub new {
    my $class = 'Event::inactivity';
    if (@_ & 1) {
	my $pk = shift;
	$class = $pk if $pk ne 'Event'; #XXX dubious
    }
    my %arg = @_;
    my $o = $class->allocate();
    $o->use_keys('e_interval', 'e_level', 'e_prev_test');
    $o->{e_repeat} = 1;
    $o->init(\%arg);
    $o->{e_interval} = 10 if !defined $o->{e_interval};
    $o->{e_level} = PRIO_NORMAL if !defined $o->{e_level};
    $o->start();
    $o;
}

sub _start {
    my ($o, $repeating) = @_;
    $o->{e_prev_test} = time;
    $o->{e_at} = time + $o->{e_interval};
}

sub _alarm {
    my ($o) = @_;
    my $qt = Event::queue_time($o->{e_level});
    if ($qt and $qt > $o->{e_prev_test}) {
	$o->{e_prev_test} = $qt;
    }
    my $left = $o->{e_prev_test} + $o->{e_interval} - time;
    if ($left > 0.0002) {  #EPSILON XXX
	$o->{e_at} = time + $left;
    } else {
	queue($o);
    }
}

1;
