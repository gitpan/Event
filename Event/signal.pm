use strict;
package Event::signal;

'Event'->register;

sub new {
    # lock %Event::

    shift;
    my %arg = @_;

    my $o = allocate();
    Event::init($o, [qw(signal)], \%arg);
    $o->start;
    $o;
}

1;
