use strict;

BEGIN {
    eval { require attrs; } or do {
	$INC{'attrs.pm'} = "";
	*attrs::import = sub {};
    }
}

package Event;
use Carp;
use base 'Exporter';
use vars qw($VERSION @EXPORT_OK @EXPORT_FAIL
	    $API $DebugLevel $Eval $DIED $Now
	    @Prepare @Check @AsyncCheck);
$VERSION = '0.21';
BOOT_XS: {
    # If we inherit DynaLoader then we inherit AutoLoader; Bletch!
    require DynaLoader;

    # DynaLoader calls dl_load_flags as a static method.
    *dl_load_flags = DynaLoader->can('dl_load_flags');

    do {
	defined(&bootstrap)
		? \&bootstrap
		: \&DynaLoader::bootstrap
    }->(__PACKAGE__);
}

$DebugLevel = 0;
$Eval = 0;		# should avoid because c_callback is exempt
$DIED = \&default_exception_handler;

@EXPORT_OK = qw(time $Now all_events all_running all_queued all_idle
		one_event sweep loop unloop unloop_all sleep queue
		QUEUES PRIO_NORMAL PRIO_HIGH R W E T);

sub export_fail {
    # Doesn't even get called!  Patch needed for Exporter.pm. XXX
    shift;
    my @fail;
    for my $sub (@_) {
	if (_load_watcher($sub)) {
	    push @EXPORT_OK, $sub;
	} else {
	    push @fail, $sub;
	}
    }
    @fail;
}

sub _load_watcher {
    my $sub = shift;
    eval { require "Event/$sub.pm" } or return;
    croak "Event/$sub.pm did not define Event::$sub\::new"
	unless defined &$sub;
    1;
}

# We use AUTOLOAD to load the Event source packages, so
# Event->process will load Event::process

sub AUTOLOAD {
    my $sub = ($Event::AUTOLOAD =~ /(\w+)$/)[0];
    _load_watcher($sub) or croak $@ . ', Undefined subroutine &' . $sub;
    goto &$sub;
}

sub default_exception_handler {
    my ($run,$err) = @_;
    my $desc = $run? $run->{desc} : '?';
    my $m = "Event: trapped error in '$desc': $err";
    $m .= "\n" if $m !~ m/\n$/;
    warn $m;
    #Carp::cluck "Event: fatal error trapped in '$desc'";
}

sub verbose_exception_handler {
    my ($run,$err) = @_;

    my $m = "Event: trapped error: $err";
    $m .= "\n" if $m !~ m/\n$/;
    return warn $m if !$run;

    $m .= " in $run --\n";
    for my $k (sort keys %$run) {
	$m .= sprintf "%18s: ", $k;
	eval {
	    my $v = $run->{$k};
	    if (!defined $v) {
		$m .= '<undef>';
	    } elsif ($v =~ /^-?\d+(\.\d+)?$/) {
		$m .= $v;
	    } else {
		$m .= "'$v'";
	    }
	};
	if ($@) { $m .= "[$@]"; $@=''; }
	$m .= "\n";
    }
    warn $m;
}

sub sweep {
    my $prio = @_ ? shift : QUEUES();
    _queue_pending();
    my $errsv = '';
    while (1) {
	eval { $@ = $errsv; _empty_queue($prio) };
	$errsv = $@;
	if ($@) {
#	    if ($Event::DebugLevel >= 2) {
#		my $e = all_running();
#		warn "Event: '$e->{desc}' died with: $@";
#	    }
	    next
	}
	last;
    }
}

use vars qw($LoopLevel $ExitLevel $Result);
$LoopLevel = $ExitLevel = 0;

sub loop {
    use integer;
    my $timeout;
    if (@_ == 1) {
	my $how_long = shift;
	$timeout = Event->timer(desc => "Event::loop timeout",
				after => $how_long,
				callback => sub { unloop($how_long) });
	$timeout->{priority} = PRIO_HIGH();
    }
    local $Result = undef;
    local $LoopLevel = $LoopLevel+1;
    ++$ExitLevel;
    my $errsv = '';
    while (1) {
	# like G_EVAL | G_KEEPERR
	eval { $@ = $errsv; _loop() };
	$errsv = $@;
	if ($@) {
#	    if ($Event::DebugLevel >= 2) {
#		my $e = all_running();
#		warn "Event: '$e->{desc}' died with: $@";
#	    }
	    next
	}
	last;
    }
    $timeout->cancel if $timeout;
    $Result;
}

sub unloop {
    $Result = shift;
    --$ExitLevel;
}

sub unloop_all { $ExitLevel = 0 }

sub add_hooks {
    shift if @_ & 1; #?
    while (@_) {
	my $k = shift;
	my $v = shift;
	$k =~ s/^\-//; # optional dash
	croak "$v must be CODE" if ref $v ne 'CODE';
	if ($k eq 'prepare') {
	    push @Event::Prepare, $v;
	} elsif ($k eq 'check') {
	    push @Event::Check, $v;
	} elsif ($k eq 'asynccheck') {
	    push @Event::AsyncCheck, $v;
	} else {
	    carp "Event: add_hooks '$k' => $v (ignored)";
	}
    }
}

#----------------------------------- backward compatibility

my $backward_noise = 20;

if (1) {
    # Do you feel like you need an entwash?  Have some of this!
    no strict 'refs';

    # Event 0.18
    *Event::queueEvent = sub {
	carp "queueEvent renamed to queue" if --$backward_noise > 0;
	&queue;
    };

    # Event 0.12
    for my $m (qw(QUEUES PRIO_HIGH PRIO_NORMAL queueEvent)) {
	*{"Event::Loop::$m"} = sub {
	    carp "$m moved to Event" if --$backward_noise > 0;
	    &$m;
	};
    }
    for my $m (qw(ACTIVE SUSPEND QUEUED RUNNING)) {
	*{"Event::$m"} = sub {
	    carp "$m moved to Event::Watcher" if --$backward_noise > 0;
	    &{"Event::Watcher::".$m};
	};
    }
    my %fix = (events => 'all_events',
	       running => 'all_running',
	       listQ => 'all_queued',
	       doOneEvent => 'one_event',
	       doEvents => 'loop',
	       Loop => 'loop',
	       exitLoop => 'unloop',
	      );
    while (my ($o,$n) = each %fix) {
	*{"Event::Loop::$o"} = sub {
	    carp "$o renamed to Event::$n" if --$backward_noise > 0;
	    &$n;
	};
    }

    # Event 0.02
    *Loop = sub {
	carp "please use Event::loop" if --$backward_noise > 0;
	&loop
    };
    *exit = sub {
	carp "please use Event::unloop" if --$backward_noise > 0;
	&unloop
    };
}

package Event::Watcher;
use base 'Exporter';
use Carp;
use vars qw(@EXPORT_OK);
@EXPORT_OK = qw(ACTIVE SUSPEND QUEUED RUNNING);

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

my $annoy = 10;
sub init {
    croak "Event::Watcher::init wants 3 args" if @_ != 3;
    local $Carp::CarpLevel = $Carp::CarpLevel + 2;
    my ($o, $keys, $arg) = @_;

    for my $up (1..4) {
	my @fr = caller $up;  # try to cope with optimized-away frames?
	next if !@fr;
	my ($file,$line) = @fr[1,2];
	$file =~ s,^.*/,,;
	$o->{desc} = "?? $file:$line";
	last;
    }

    for (@$keys, qw(repeat reentrant desc callback debug)) {
	if (exists $arg->{"-$_"}) {
	    carp "Please remove leading dashes" if --$annoy > 0;
	    $o->{$_} = $arg->{"-$_"} 
	} elsif (exists $arg->{$_}) {
	    $o->{$_} = $arg->{$_};
	}
    }
    do {
	no strict 'refs';
	$o->{priority} = $ { ref($o)."::DefaultPriority" } ||
	    Event::PRIO_NORMAL();
    };
    if (exists $arg->{'-priority'} || exists $arg->{priority}) {
	carp "'priority' has been renamed to 'nice'";
	$o->{priority} += $arg->{"-priority"} || $arg->{priority} || 0;
    }
    $o->{priority} += $arg->{"-nice"} || $arg->{nice} || 0;

    if ($arg->{async} || $arg->{'-async'}) {
	# I like async!
	#carp "async is depreciated";
	$o->{priority} = -1;
    }

    Carp::cluck "creating ".ref($o)." desc='$o->{desc}'\n"
	if $Event::DebugLevel >= 3;
    
    $o;
}

package Event::Stats;
use base 'Exporter';
use vars qw(@EXPORT_OK);
@EXPORT_OK = qw(MAXTIME);

# restart & DESTROY methods are implemented in XS

1;
