use strict;

BEGIN {
    eval { require attrs; } or do {
	$INC{'attrs.pm'} = "";
	*attrs::import = sub {};
    }
}

package Event;
use Carp qw(carp cluck croak confess);
use Time::HiRes qw(time);  #can be optional? XXX
use vars qw($VERSION $DebugLevel %Set);
$VERSION = '0.05';

# 0    FAST, FAST, FAST!
# 1    COLLECT SOME STATISTICS
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
	$o->{queued} = 0;
    }
    
    $_DEBUGLEVEL = $DebugLevel if !defined $_DEBUGLEVEL;
    croak "\$DebugLevel cannot be changed at run-time"
	if $_DEBUGLEVEL != $DebugLevel;

    if ($DebugLevel) {
	croak "re-initialized" if $o->{initialized};
	$o->{initialized} = 1;

	# pick a style and stick with it!
	my @old = grep /^-/, keys %$o;
	carp "noticed old style keys (".join(',',@old).")"
	    if @old && ++$warn_old_style < 3;

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

sub cancel {
    my $e = shift;
    $e->{'cancelled'}=1;
    delete $Set{ 0+$e } if $DebugLevel;
}

#----------- these should only be called if $DebugLevel >= 1
sub Status {
    warn sprintf("\n%7s %-40s\n", 'WHAT', 'DESCRIPTION')."\n";
    for my $e (sort { $b->{elapse} <=> $a->{elapse} } values %Set) {
	my $name = ref $e;
	$name =~ s/^Event:://;
	warn sprintf("%7s %-40s %4d %6.2f %6.2f\n", $name, $e->{desc}, $e->{ran},
		     $e->{total_elapse}, $e->{elapse});
    }
}

my $start_time;
sub invoking {
    my ($e) = @_;
    if (!exists $Set{ 0+$e }) {
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

    croak "Cannot find Event package Event::${sub}"
	unless defined &{$Event::AUTOLOAD};

    goto &{$Event::AUTOLOAD};
}

use vars qw(%Source @Sources @AsyncSources);
sub _register {
    no strict 'refs';
    my $package = shift;

    die "$package is already loaded" if $Source{$package};
    $Source{$package} = 1;

    if (! $package->isa('Event')) {
#	carp "\@$package\::ISA must include Event";
	unshift @{"$package\::ISA"}, 'Event';
    }

    my $name = $package;
    $name =~ s/^.*:://;
    my $sub = \&{"$package\::new"};
    die "can't find $package\::new"
	if !$sub;
    *{$name} = $sub;
}

sub register {
    my $package = caller;
    _register($package);
    push @Sources, $package;
}
sub registerAsync {
    my $package = caller;
    _register($package);
    push @AsyncSources, $package;
}

#----------------------------------- Event 0.02 compatibility

sub Loop {
    confess "please use Event::Loop::Loop"
	if shift ne 'Event';
    &Event::Loop::Loop;
}

sub exit { shift; &Event::Loop::exitLoop }

package Event::Loop;
use Carp;
use builtin qw(min);
use vars qw(@ISA @EXPORT_OK
	    @Queue $queueCount @Idle);
@ISA = 'Exporter';
@EXPORT_OK = qw(initQueue waitForEvents queueEvent emptyQueue
		doOneEvent Loop exitLoop
		QUEUES PRIO_HIGH PRIO_NORMAL);

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

sub initQueue {
    for (0 .. QUEUES-1) { $Queue[$_] = [] }
    $queueCount = 0;
    @Idle = ();
}
initQueue();

#--------------------------------------- Queue

sub queueEvent {
    # OPTIMIZE (1)

    # Probably can be more efficient with linked-lists implemented
    # in C.  This may necessitate restricting each event instance
    # to be queued at most once.

    my $prio = shift;
    push @{$Queue[$prio]}, @_;
    $queueCount += @_;

    if ($Event::DebugLevel >= 3) {
	my $pk = caller;
	warn "Event: queuing event(s) from $pk at priority $prio\n";
    }
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

	    (shift @$q)->();
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

    my $wait = $queueCount ? 0 : 3600;
    $wait = min $wait, (map { $_->prepare } @Event::Sources);

    warn "Event::waitForEvents: wait=$wait\n"
	if $Event::DebugLevel >= 3;
    Event::OS::WaitForEvent($wait);

    for my $e (@Event::Sources) { $e->check }

    if ($wait) {
	for my $e (@Event::AsyncSources) { $e->check }
    }
}

sub doOneEvent {
    # OPTIMIZE (2)

    for my $e (@Event::AsyncSources) { $e->check }

    return 1 if emptyQueue();

    waitForEvents();

    return 1 if emptyQueue();

    while (my $idle = shift @Idle) {
	$idle->{queued} = 0;
	next if $idle->{'cancelled'};

	if (!$Event::DebugLevel) {
	    $idle->{'callback'}->($idle);
	} else {
	    Event::invoking($idle);
	    $idle->{'callback'}->($idle);
	    Event::completed($idle);
	}
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

    croak "idle event has no callback" unless $arg{callback};

    # Fancy scheduling of idle events can be accomplished by
    # routing all your idle time through the fancy idle scheduler
    # of your choice.
    $arg{priority} = Event::Loop::QUEUES + 1;  #fool init

    my $o = bless \%arg, __PACKAGE__;
    Event::init($o)->again;
    $o;
}

sub prepare {
    return 3600 if !@Idle;
    0

# Hm...?
#    min map {
#	my $x = $_->can('prepare');
#	$x? $x->prepare : 3600;
#    } @Idle;

}

sub check {}

sub again {
    my $o = shift;
    return if $o->{queued};
    $o->{queued} = 1;
    push @Idle, $o;
}

1;
