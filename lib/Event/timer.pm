use strict;
package Event::timer;
use Carp;
use base 'Event::Watcher';

'Event::Watcher'->register;

sub new {
#    lock %Event::;

    shift if @_ & 1;
    my %arg = @_;
    my $o = allocate();

    my $has_at = exists $arg{e_at};
    my $has_after = exists $arg{e_after};

    croak "'e_after' and 'e_at' are mutually exclusive"
	if $has_at && $has_after;

    if ($has_after) {
	my $after = delete $arg{e_after};
	$o->{e_at} = Event::time() + $after;
	$has_at=1;
	$o->{e_interval} = $after if !exists $arg{e_interval};
    } elsif ($has_at) {
	$o->{e_at} = delete $arg{e_at};
    }
    if (exists $arg{e_interval}) {
	my $i = delete $arg{e_interval};
	$o->{e_at} = Event::time() + (ref $i? $$i : $i) unless $has_at;
	$o->{e_interval} = $i;
	$o->{e_repeat} = 1 unless exists $arg{e_repeat};
    }

    $o->init(\%arg);
    $o->start();
    $o;
}

1;
