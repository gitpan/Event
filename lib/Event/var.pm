use strict;
package Event::var;
use base 'Event::Watcher';

'Event::Watcher'->register;

sub new {
    # lock %Event::;

    shift if @_ & 1;
    my %arg = @_;

    my $o = allocate();

    $o->init(\%arg);
    $o->start;
    $o;
}

1;
