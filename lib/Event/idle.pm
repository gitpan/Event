use strict;
package Event::idle;
use Carp;
use base 'Event::Watcher';
use vars qw($DefaultPriority);

'Event::Watcher'->register;

sub new {
#    lock %Event::;

    shift if @_ & 1;
    my %arg = @_;

    my $o = allocate();
    $o->init([qw(min_interval max_interval hard)], \%arg);
    $o->{repeat} = 1 if (!exists $arg{repeat} and
			 (defined $o->{min_interval} or
			  defined $o->{max_interval}));
    $o->start;
    $o;
}

1;
