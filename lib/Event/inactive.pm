use strict;
package Event::inactive;
use Carp;
use Event qw(PRIO_HIGH PRIO_NORMAL time $Now queue);
use base 'Event::Watcher';
use vars qw($DefaultPriority);
$DefaultPriority = PRIO_NORMAL() + 1;

'Event::Watcher'->register();

sub new {
    shift if @_ & 1;
    my %arg = @_;
    my $o = 'Event::inactive'->allocate();
    $o->{repeat} = 1;
    $o->init([qw(interval level)], \%arg);
    $o->{interval} = 10 if !exists $o->{interval};
    $o->{level} = PRIO_NORMAL() if !exists $o->{level};
    $o->start();
    $o;
}

sub _start {
    my ($o, $repeating) = @_;
    $o->{_qt} = $Now;
    $o->{at} = $Now + $o->{interval};
}

sub _alarm {
    my ($o) = @_;
    my $qt = Event::queue_time($o->{level});
    if ($qt and $qt > $o->{_qt}) {
	$o->{_qt} = $qt;
    }
    my $left = $o->{_qt} + $o->{interval} - $Now;
    if ($left > 0) {  #EPSILON XXX
	$o->{at} = $Now + $left;
    } else {
	queue($o);
    }
}

1;
