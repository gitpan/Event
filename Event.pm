use strict;

BEGIN {
    eval { require attrs; } or do {
	$INC{'attrs.pm'} = "";
	*attrs::import = sub {};
    }
}

package Event;
require Exporter;
*require_version = \&Exporter::require_version;
use Carp qw(carp cluck croak confess);
use Time::HiRes qw(time);  #can be optional? XXX
use vars qw($VERSION $DebugLevel %Set);
$VERSION = '0.07';

# 0    FAST, FAST, FAST!
# 1    COLLECT SOME NICE STATISTICS
# 2    MINIMAL DEBUGGING OUTPUT
# 3    EXCESSIVE DEBUGGING OUTPUT

$DebugLevel = 0;

BOOT_XS: {
    # If I inherit DynaLoader then I inherit AutoLoader; Bletch!
    require DynaLoader;

    # DynaLoader calls dl_load_flags as a static method.
    *dl_load_flags = DynaLoader->can('dl_load_flags');

    do {
	defined(&bootstrap)
		? \&bootstrap
		: \&DynaLoader::bootstrap
    }->(__PACKAGE__);
}

my $_DEBUGLEVEL;
my $warn_old_style=0;
sub init {
    croak "Event::init wants 1 arg" if @_ != 1;
    local $Carp::CarpLevel = 1;
    my ($o) = @_;

    # sanity checks (?breaks abstraction)
    croak "no priority" if !exists $o->{priority};
    croak "no callback" if !exists $o->{callback};

    $o->{cancelled} = 0;
    if ($o->can('again')) {
	$o->{repeat} = 0 if !exists $o->{repeat};
	$o->{active} = 0;
    }
    
    $_DEBUGLEVEL = $DebugLevel if !defined $_DEBUGLEVEL;
    croak "\$DebugLevel cannot be changed at run-time"
	if $_DEBUGLEVEL != $DebugLevel;

    if ($DebugLevel) {
	croak "re-initialized" if $o->{initialized};
	$o->{initialized} = 1;

	# pick a style and stick with it!
#	my @old = grep /^-/, keys %$o;
#	carp "noticed old style keys (".join(',',@old).")"
#	    if @old && ++$warn_old_style < 3;

	cluck "Event::init ".ref($o)." $o->{desc}\n"
	    if ($DebugLevel >= 2 or ($DebugLevel and !$o->{desc}));

	my $where;
	for my $up (1..4) {
	    my @fr = caller $up;  # try to cope with optimized-away frames
	    next if !@fr;
	    my ($file,$line) = @fr[1,2];
	    $file =~ s,^.*/,,;
	    $where = "$file:$line";
	}
	$where ||= '?';
	$o->{desc} ||= $where;

	for (qw(ran elapse total_elapse)) {
	    $o->{$_} = 0;
	}
	$Set{ 0+$o } = $o;
    }
    $o;
}

*now = \&Event::Loop::queueEvent;

sub cancel {
    my $e = shift;
    $e->{'cancelled'}=1;
    delete $Set{ 0+$e } if $DebugLevel;
}

#----------- these should only be called if $DebugLevel >= 1

sub Status {
    # might be renamed XXX
    my $buf='';
    $buf.= sprintf("\n%7s %-40s\n", 'WHAT', 'DESCRIPTION')."\n";
    for my $e (sort { $b->{elapse} <=> $a->{elapse} } values %Set) {
	my $name = ref $e;
	$name =~ s/^Event:://;
	$buf.= sprintf("%7s %-40s %4d %6.2f %6.2f\n", $name, $e->{desc},
		       $e->{ran}, $e->{total_elapse}, $e->{elapse});
    }
    warn $buf;
}

my $start_time;
sub invoking {
    my ($e) = @_;
    if (!exists $Set{ 0+$e }) {
	# debugging
	require Data::Dumper;
	warn "bogus event in queue:\n".Data::Dumper::Dumper($e);
	return;
    }
    warn "Event: invoking ".ref($e)." $e->{desc}\n"
	if $DebugLevel >= 2;
    $start_time = time;
}

sub completed {
    my ($e) = @_;
    return if !exists $Set{ 0+$e };
    warn "Event: completed ".ref($e)." $e->{desc}\n"
	if $DebugLevel >= 3;
    ++$e->{ran};
    $e->{elapse} = time - $start_time;
    $e->{total_elapse} += $e->{elapse};
}


# We use AUTOLOAD to load the Event source packages, so
# Event->process will load Event::process and create a new
# sub which will call Event::process->new(@_);

sub AUTOLOAD {
    my $sub = ($Event::AUTOLOAD =~ /(\w+)$/)[0];

    eval { require "Event/" . $sub . ".pm" }
	or croak $@ . ', Undefined subroutine &' . $Event::AUTOLOAD;

    croak "Event/$sub.pm did not define Event::${sub}"
	unless defined &{$Event::AUTOLOAD};

    goto &{$Event::AUTOLOAD};
}

sub register {
    no strict 'refs';
    my $package = caller;

    unshift @{"$package\::ISA"}, 'Event'
	if !$package->isa('Event');

    my $name = $package;
    $name =~ s/^.*:://;

    my $sub = \&{"$package\::new"};
    die "can't find $package\::new"
	if !$sub;

    *{$name} = $sub;

    shift;
    while (@_) {
	my $k = shift;
	if ($k eq 'prepare') {
	    push @Event::Loop::Prepare, shift;
	} elsif ($k eq 'check') {
	    push @Event::Loop::Check, shift;
	} elsif ($k eq 'asynccheck') {
	    push @Event::Loop::AsyncCheck, shift;
	} else {
	    carp "Event: register '$k' => ".shift()." (ignored)";
	}
    }
}

#----------------------------------- Event 0.02 compatibility

sub Loop {
    confess "please use Event::Loop::Loop"
	if shift ne 'Event';
    &Event::Loop::Loop;
}
sub exit {
    confess "please use Event::Loop::exitLoop"
	if shift ne 'Event';
    &Event::Loop::exitLoop
}

package Event::Loop;
use Carp;
use builtin qw(min);
use vars qw(@ISA @EXPORT_OK
	    @Queue $queueCount @Idle $Now @Prepare @Check @AsyncCheck);
@ISA = 'Exporter';
@EXPORT_OK = qw(waitForEvents queueEvent emptyQueue doOneEvent Loop exitLoop
		$Now @Prepare @Check @AsyncCheck QUEUES PRIO_HIGH PRIO_NORMAL);

if (eval "require Event::OS::" . $^O ) {
    #ok
}
elsif ($@ =~ /Can\'t locate/ ) {
    require Event::OS::default;  #hope ok
}
else { die }

# Hard to imagine a need for more than 10 queues...
sub QUEUES() { 10 }

sub PRIO_HIGH() { 3 }
sub PRIO_NORMAL() { 6 }

for (0 .. QUEUES-1) { $Queue[$_] = [] }
$queueCount = 0;
@Idle = ();

#--------------------------------------- Queue

sub _invoke_event {
    my $arg = shift;
    my $e = $arg->[0];
    my $cb = $e->{callback};

    Event::invoking($e)
	if $Event::DebugLevel;

    if (ref $cb eq 'CODE') {
	$cb->(@$arg);
    } else {
	my ($obj,$m) = @$cb; #ignore rest of array? XXX
	$obj->$m(@$arg);
    }

    Event::completed($e)
	if $Event::DebugLevel;
}

sub queueEvent {
    # OPTIMIZE (1)

    # Probably can be more efficient with linked-lists implemented
    # in C.  This may necessitate restricting each event instance
    # to be queued at most once.

    use integer;
    my @arg = @_;
    my $e = $_[0];
    my $prio = $e->{priority};

    if ($prio < 0) {
	warn "Event: calling $e->{desc} asyncronously (priority $prio)\n"
	    if $Event::DebugLevel >= 3;
	_invoke_event(\@arg);
	return;
    }

    $prio = QUEUES-1 if $prio >= QUEUES;
    warn "Event: queuing $e->{desc} at priority $prio\n"
	if $Event::DebugLevel >= 3;
    push @{$Queue[$prio]}, \@arg;
    ++$queueCount;
}

sub emptyQueue {
    # OPTIMIZE (1)

    use integer;
    my ($max) = @_;
    if (!defined $max) {
	$max = QUEUES 
    } else {
	$max = QUEUES if $max > QUEUES;
    }
    for (my $pri = 0; $pri < $max; $pri++) {
	my $q = $Queue[$pri];
	if (@$q) {
	    # This might queue more events, so we must restart the
	    # search from the beginning.

	    _invoke_event(shift @$q);
	    $queueCount--;
	    return 1
	}
    }
    0
}

sub waitForEvents {
    # OPTIMIZE (2)

    # prepare can probably be reduced to checking:
    #   $queueCount, next timer, and @Idle

    my $wait = $queueCount + @Idle ? 0 : 3600;
    $wait = min $wait, (map { $_->() } @Prepare);

    warn "Event::waitForEvents: wait=$wait\n"
	if $Event::DebugLevel >= 3;
    Event::OS::WaitForEvent($wait);

    for my $x (@Check) { $x->() }

    if ($wait) {
	# We only need to re-check if WaitForEvent actually
	# waited for a "non-zero" amount of time.
	for my $x (@AsyncCheck) { $x->() }
    }
}

sub doOneEvent {
    # OPTIMIZE (2)

    for my $x (@AsyncCheck) { $x->() }

    return 1 if emptyQueue();

    waitForEvents();

    return 1 if emptyQueue();

    while (my $idle = shift @Idle) {
	$idle->{active} = 0;
	next if $idle->{'cancelled'};
	_invoke_event([$idle]);
	$idle->again if $idle->{repeat};
	return 1;
    }
    0;
}

#--------------------------------------- Loop

use vars qw($LoopLevel $ExitLevel $Result);
$LoopLevel = $ExitLevel = 0;

sub Loop {
    use integer;
    local $Result = 'abnormal';
    local $LoopLevel = $LoopLevel+1;
    ++$ExitLevel;
    doOneEvent() while $ExitLevel >= $LoopLevel;
    $Result;
}

sub exitLoop {
    $Result = shift;
    --$ExitLevel;
}

#--------------------------------------- idle

package Event::idle;
use Carp;
use builtin qw(min);
use vars qw(@Idle);
*Idle = \@Event::Loop::Idle;

'Event'->register;

my $arg_warning=0;
sub new {
#    lock %Event::;

    shift;
    my %arg = (repeat => 0);
    if (@_ == 1) {
	$arg{callback} = shift;
	carp "pls change to Event->idle(callback => \$callback)"
	    if ++$arg_warning < 3;
    }
    else { %arg = @_ }

    for (qw(callback)) {
	$arg{$_} = $arg{"-$_"} if exists $arg{"-$_"};
    }

    $arg{priority} = Event::Loop::PRIO_HIGH; # or normal?

    my $o = bless \%arg, __PACKAGE__;
    Event::init($o)->again;
    $o;
}

sub again {
    my $o = shift;
    return if $o->{active};
    $o->{active} = 1;
    push @Idle, $o;
}

1;
