use strict;
package Event::signal;
use Carp;
use vars qw($DefaultPriority);
$DefaultPriority = Event::Loop::PRIO_HIGH();

'Event'->register;

sub new {
    # lock %Event::

    shift;
    my %arg = @_;

    my $o = allocate();
    Event::init($o, [qw(signal)], \%arg);
#    confess "huh?" if ! $o->{signal}; XXX
    $o->start;
    $o;
}

1;
