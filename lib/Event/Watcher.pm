use strict;
package Event::Watcher;
use base 'Exporter';
use Carp;
use vars qw(@EXPORT_OK);
@EXPORT_OK = qw(ACTIVE SUSPEND QUEUED RUNNING R W E T);

sub register {
    no strict 'refs';
    my $package = caller;

    my $name = $package;
    $name =~ s/^.*:://;

    my $sub = \&{"$package\::new"};
    die "can't find $package\::new"
	if !$sub;
    *{"Event::".$name} = $sub;

    &Event::add_hooks if @_;
}

my $warn_noise = 10;
sub init {
    croak "Event::Watcher::init wants 2 args" if @_ != 2;
    my ($o, $arg) = @_;

    for my $k (keys %$arg) { #deprecated
	if ($Event::KEY_REMAP{$k}) {
	    Carp::cluck "'$k' is renamed to '$Event::KEY_REMAP{$k}'"
		if --$warn_noise >= 0;
	    $arg->{ $Event::KEY_REMAP{$k} } = delete $arg->{$k};
	}
    }

    if (!exists $arg->{e_desc}) {
	# try to find caller but cope with optimized-away frames & etc
	for my $up (1..4) {
	    my @fr = caller $up;
	    next if !@fr || $fr[0] =~ m/^Event\b/;
	    my ($file,$line) = @fr[1,2];
	    $file =~ s,^.*/,,;
	    $o->{e_desc} = "?? $file:$line";
	    last;
	}
    }

    # find e_prio
    if (exists $arg->{e_prio}) {
	$o->{e_prio} = delete $arg->{e_prio}
    } elsif ($arg->{e_async}) {
	delete $arg->{e_async};
	$o->{e_prio} = -1;
    } else {
	no strict 'refs';
	$o->{e_prio} = $ { ref($o)."::DefaultPriority" } ||
	    Event::PRIO_NORMAL();
	if (exists $arg->{e_nice}) {
	    $o->{e_prio} += delete $arg->{e_nice}
	}
    }

    $o->{$_} = $arg->{$_} for keys %$arg;

    Carp::cluck "creating ".ref($o)." desc='$o->{e_desc}'\n"
	if $Event::DebugLevel >= 3;
    
    $o;
}

1;
