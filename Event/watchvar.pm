use strict;
package Event::watchvar;

'Event'->register;

sub new {
    # lock %Event::;

    shift;
    my %arg = @_;

    my $o = allocate();

    Event::init($o, ['variable'], \%arg);
    $o->start;
    $o;
}

1;
