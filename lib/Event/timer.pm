use strict;
package Event::timer;
use Carp;
use Time::HiRes qw(time);

'Event'->register;

sub new {
#    lock %Event::;

    shift;
    my %arg = @_;
    my $o = allocate();
    Event::init($o, [qw(hard)], \%arg);

    for (qw(after at interval)) {
	$arg{$_} = $arg{"-$_"} if exists $arg{"-$_"};
    }

    if (exists $arg{after}) {
	croak "'after' and 'at' are mutually exclusive"
	    if exists $arg{at};
	$o->{at} = time() + $arg{after};
	$o->{interval} = $arg{after} if !exists $arg{interval};
    }
    elsif (exists $arg{at}) {
	$o->{at} = 0 + $arg{at};
    }

    if (exists $arg{interval}) {
	my $i = $arg{interval};
	$o->{at} = time() + (ref $i? $$i : $i) unless $arg{at};
	$o->{interval} = $i;
	$o->{repeat} = 1;
    }

    $o->start();
    $o;
}

1;
