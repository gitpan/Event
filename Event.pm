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
use vars qw($VERSION $DebugLevel $Eval $Stats);
$VERSION = '0.08';

# 0    FAST, FAST, FAST!
# 1    COLLECT SOME NICE STATISTICS
# 2    MINIMAL DEBUGGING OUTPUT
# 3    EXCESSIVE DEBUGGING OUTPUT

$DebugLevel = 0;
$Eval = 0;
$Stats = 0;

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

sub init {
    croak "Event::init wants 3 args" if @_ != 3;
    my ($o, $keys, $arg) = @_;

    for (@$keys, qw(repeat desc callback debug)) {
	if (exists $arg->{"-$_"}) {
	    $o->{$_} = $arg->{"-$_"} 
	} elsif (exists $arg->{$_}) {
	    $o->{$_} = $arg->{$_};
	}
    }
    $o->{priority} += $arg->{"-priority"} || $arg->{priority} || 0;
    $o->{priority} = -1
	if $arg->{async} || $arg->{'-async'};

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

    cluck "creating ".ref($o)." desc='$o->{desc}'\n"
	if $DebugLevel >= 3;
    
    $o;
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
	    @Queue $queueCount $Now @Prepare @Check @AsyncCheck);
@ISA = 'Exporter';
@EXPORT_OK = qw(queueEvent emptyQueue doOneEvent Loop exitLoop
		$Now @Prepare @Check @AsyncCheck QUEUES PRIO_HIGH PRIO_NORMAL);

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

'Event'->register;

my $arg_warning=0;
sub new {
#    lock %Event::;

    shift;
    my %arg;
    if (@_ == 1) {
	$arg{callback} = shift;
	carp "pls change to Event->idle(callback => \$callback)"
	    if ++$arg_warning < 3;
    }
    else { %arg = @_ }

    my $o = Event::init(allocate(), [], \%arg);
    $o->again;
    $o;
}

1;
