use strict;
package Event::signal;
use Carp;
use base 'Event::Watcher';
use vars qw($DefaultPriority @ATTRIBUTE);
$DefaultPriority = Event::PRIO_HIGH();
@ATTRIBUTE = qw(signal);

'Event::Watcher'->register;

sub new {
    # lock %Event::

    my $o = allocate(shift);
    my %arg = @_;
    $o->init(\%arg);
    $o->start;
    $o;
}

1;
