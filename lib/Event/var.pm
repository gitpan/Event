use strict;
package Event::var;
use base 'Event::Watcher';
use vars qw(@ATTRIBUTE);
@ATTRIBUTE = qw(var poll);

'Event::Watcher'->register;

sub new {
    # lock %Event::;

    my $o = allocate(shift);
    my %arg = @_;
    $o->init(\%arg);
    $o->start;
    $o;
}

1;
