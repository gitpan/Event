use strict;
package Event::timer;
use Carp;

'Event::Watcher'->register;

sub new {
#    lock %Event::;

    shift if @_ & 1;
    my %arg = @_;
    my $o = allocate();
    $o->init([qw(hard)], \%arg);

    for (qw(after at interval)) {
	$arg{$_} = $arg{"-$_"} if exists $arg{"-$_"};
    }

    if (exists $arg{after}) {
	croak "'after' and 'at' are mutually exclusive"
	    if exists $arg{at};
	$o->{at} = Event::time() + $arg{after};
	$o->{interval} = $arg{after} if !exists $arg{interval};
    }
    elsif (exists $arg{at}) {
	$o->{at} = 0 + $arg{at};
    }

    if (exists $arg{interval}) {
	my $i = $arg{interval};
	$o->{at} = Event::time() + (ref $i? $$i : $i) unless $arg{at};
	$o->{interval} = $i;
	$o->{repeat} = 1;
    }

    $o->start();
    $o;
}

1;
