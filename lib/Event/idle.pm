use strict;
package Event::idle;
use Carp;
use base 'Event::Watcher';
use vars qw($DefaultPriority);
use Event qw(%KEY_REMAP);

'Event::Watcher'->register;

sub new {
#    lock %Event::;

    shift if @_ & 1;
    my %arg = @_;
    my $o = allocate();
    $o->{e_repeat} = 1 if (defined $o->{e_min} or defined $o->{e_max});
    $o->init(\%arg);
    $o->start;
    $o;
}

1;
