use strict;
package Event::signal;
use Carp;
use base 'Event::Watcher';
use vars qw($DefaultPriority);
$DefaultPriority = Event::PRIO_HIGH();

'Event::Watcher'->register;

sub new {
    # lock %Event::

    shift if @_ & 1;
    my %arg = @_;

    my $o = allocate();
    $o->init([qw(signal)], \%arg);
#    confess "huh?" if ! $o->{signal}; XXX
    $o->start;
    $o;
}

1;
