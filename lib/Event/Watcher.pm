use strict;
package Event::Watcher;
use base 'Exporter';
use Carp;
use vars qw(@EXPORT_OK @ATTRIBUTE);
@EXPORT_OK = qw(ACTIVE SUSPEND QUEUED RUNNING R W E T);
@ATTRIBUTE = qw(cb cbtime clump desc debug prio reentrant repeat max_cb_tm);

sub register {
    no strict 'refs';
    my $package = caller;

    my $name = $package;
    $name =~ s/^.*:://;

    my $sub = \&{"$package\::new"};
    die "can't find $package\::new"
	if !$sub;
    *{"Event::".$name} = sub {
	shift;
	$sub->("Event::".$name, @_);
    };

    &Event::add_hooks if @_;
}

my $warn_noise = 10;
sub init {
    croak "Event::Watcher::init wants 2 args" if @_ != 2;
    my ($o, $arg) = @_;

    for my $k (keys %$arg) {
	if ($k =~ s/^e_//) {
	    Carp::cluck "'e_$k' is renamed to '$k'"
		if --$warn_noise >= 0;
	    $arg->{$k} = delete $arg->{"e_$k"};
	}
    }

    if (!exists $arg->{desc}) {
	# try to find caller but cope with optimized-away frames & etc
	for my $up (1..4) {
	    my @fr = caller $up;
	    next if !@fr || $fr[0] =~ m/^Event\b/;
	    my ($file,$line) = @fr[1,2];
	    $file =~ s,^.*/,,;
	    $o->desc("?? $file:$line");
	    last;
	}
    }

    # find e_prio
    if (exists $arg->{prio}) {
	$o->prio(delete $arg->{prio})
    } elsif ($arg->{async}) {
	delete $arg->{async};
	$o->prio(-1);
    } else {
	no strict 'refs';
	$o->prio($ { ref($o)."::DefaultPriority" } || Event::PRIO_NORMAL);
	if (exists $arg->{nice}) {
	    $o->prio($o->prio + delete $arg->{nice});
	}
    }

    for my $k (keys %$arg) {
	my $m = $k;
	if ($o->can($m)) {
	    $o->$m($arg->{$k});
	    next;
	}
	carp "Setting non-event fields in the constructor is deprecated ($k)";
	$o->{$k} = $arg->{$k};
    }

    Carp::cluck "creating ".ref($o)." desc='".$o->desc."'\n"
	if $Event::DebugLevel >= 3;
    
    $o;
}

sub attributes {
    no strict 'refs';
    my ($o) = @_;
    my $pk = ref $o? ref $o : $o;
    @{"$ {pk}::ATTRIBUTE"}, map { attributes($_) } @{"$ {pk}::ISA"};
}

sub configure {
    my $o = shift;
    if (! @_) {
	map { $_, $o->$_() } $o->attributes;
    } else {
	while (my ($k,$v)= splice @_, -2) { $o->$k($v)}
	1 # whatever
    }
}

package Event::Watcher::Tied;
use vars qw(@ISA @ATTRIBUTE);
@ISA = 'Event::Watcher';
@ATTRIBUTE = qw(hard at flags);

1;
