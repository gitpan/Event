use strict;
package Event::io;

'Event'->register;

sub new {
#    lock %Event::;

    shift;
    my %arg = @_;

    my $o = allocate();
    Event::init($o, [qw(handle events)], \%arg);
    $o->start;
    $o;
}

1;
