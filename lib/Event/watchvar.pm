use strict;
package Event::watchvar;
use base 'Event::Watcher';

'Event::Watcher'->register;

sub new {
    # lock %Event::;

    shift if @_ & 1;
    my %arg = @_;

    my $o = allocate();

    $o->init(['variable'], \%arg);
    $o->start;
    $o;
}

1;
