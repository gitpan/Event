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
use vars qw($VERSION $API $DebugLevel $Eval $DIED);
$VERSION = '0.12';
$Eval = 0;
$DIED = sub {
    my ($run, $err) = @_;
    my $desc = $run? $run->{desc} : '?';
    warn "Event: fatal error trapped in '$desc': $err";
};

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

sub init {
    croak "Event::init wants 3 args" if @_ != 3;
    my ($o, $keys, $arg) = @_;

    for my $up (1..4) {
	my @fr = caller $up;  # try to cope with optimized-away frames?
	next if !@fr;
	my ($file,$line) = @fr[1,2];
	$file =~ s,^.*/,,;
	$o->{desc} = "?? $file:$line";
	last;
    }

    for (@$keys, qw(repeat desc callback debug)) {
	if (exists $arg->{"-$_"}) {
	    $o->{$_} = $arg->{"-$_"} 
	} elsif (exists $arg->{$_}) {
	    $o->{$_} = $arg->{$_};
	}
    }
    do {
	no strict 'refs';
	$o->{priority} = $ { ref($o)."::DefaultPriority" } ||
	    Event::Loop::PRIO_NORMAL();
    };
    $o->{priority} += $arg->{"-priority"} || $arg->{priority} || 0;
    $o->{priority} = -1
	if $arg->{async} || $arg->{'-async'};

    cluck "creating ".ref($o)." desc='$o->{desc}'\n"
	if $DebugLevel >= 3;
    
    $o;
}


# We use AUTOLOAD to load the Event source packages, so
# Event->process will load Event::process and create a new
# sub which will call Event::process->new(@_);

sub AUTOLOAD {
    my $sub = ($Event::AUTOLOAD =~ /(\w+)$/)[0];

    eval { require "Event/$sub.pm" }
	or croak $@ . ', Undefined subroutine &' . $sub;

    croak "Event/$sub.pm did not define Event::$sub"
	unless defined &$sub;

    goto &$sub;
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

    &add_hooks;
}

sub add_hooks {
    shift;
    while (@_) {
	my $k = shift;
	my $v = shift;
	$k =~ s/^\-//; # optional dash
	croak "$v must be CODE" if ref $v ne 'CODE';
	if ($k eq 'prepare') {
	    push @Event::Loop::Prepare, $v;
	} elsif ($k eq 'check') {
	    push @Event::Loop::Check, $v;
	} elsif ($k eq 'asynccheck') {
	    push @Event::Loop::AsyncCheck, $v;
	} else {
	    carp "Event: add_hooks '$k' => $v (ignored)";
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

package Event::Stats;

# restart & DESTROY methods are implemented in XS

package Event::Loop;
use Carp;
use builtin qw(min);
use vars qw(@ISA @EXPORT_OK
	    @Queue $queueCount $Now @Prepare @Check @AsyncCheck);
@ISA = 'Exporter';
@EXPORT_OK = qw(queueEvent doEvents doOneEvent Loop exitLoop
		$Now @Prepare @Check @AsyncCheck QUEUES PRIO_HIGH PRIO_NORMAL);

#--------------------------------------- Loop

use vars qw($LoopLevel $ExitLevel $Result);
$LoopLevel = $ExitLevel = 0;

sub Loop {
    use integer;
    local $Result = 'abnormal';
    local $LoopLevel = $LoopLevel+1;
    ++$ExitLevel;
    while (1) {
	eval { doEvents() };
	if ($@) {
	    my $err = $@;
	    $Event::DIED->(scalar running(), $err);
	    next;
	}
	last;
    }
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
