use strict;
package Event::idle;
use Carp;
use base 'Event::Watcher';
use vars qw($DefaultPriority @ATTRIBUTE);

@ATTRIBUTE = qw(hard max min);

'Event::Watcher'->register;

sub new {
#    lock %Event::;

    my $o = allocate(shift);
    my %arg = @_;
    
    # deprecated
    for (qw(min max repeat)) {
	if (exists $arg{"e_$_"}) {
	    carp "'e_$_' is renamed to '$_'";
	    $arg{$_} = delete $arg{"e_$_"};
	}
    }

    $o->repeat(1) if defined $arg{min} || defined $arg{max};
    $o->init(\%arg);
    $o->start;
    $o;
}

1;
