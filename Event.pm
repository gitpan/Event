
package Event;

use strict;
use vars qw(@ISA $VERSION);
use Carp;

$VERSION = "0.01";

BOOT_XS: {
    # If I inherit DynaLoader then I inherit AutoLoader and I DON'T WANT TO
    require DynaLoader;

    # DynaLoader calls dl_load_flags as a static method.
    *dl_load_flags = DynaLoader->can('dl_load_flags');

    do {
	defined(&bootstrap)
		? \&bootstrap
		: \&DynaLoader::bootstrap
    }->(__PACKAGE__);
}

if(eval "require Event::OS::" . $^O ) {
    @Event::OS::ISA = ( "Event::OS::" . $^O );
}
elsif( $@ =~ /Can't locate Event/ ) {
    require Event::OS::default;
    @Event::OS::ISA = qw(Event::OS::default);
}
else {
    die $@;
}

my @queue;		# the queues
my %source = ();	# Event source package names
my %asyncsource = ();	# AsyncEvent source package names
my $maxSleep = 60;	# maximum sleep time
my $exit = undef;	# set to exit event loop
my $idleCount;		# max number of idle events to process
my $asyncCount;		# max number of async events to process
my $queueCount;		# total number of queued events
my @atexit = ();	# list of callbacks to call on exit

sub IDLE_QUEUE    () {  0 }
sub DEFAULT_QUEUE () {  5 }
sub ASYNC_QUEUE   () { 10 }

INIT: {
    for (IDLE_QUEUE .. ASYNC_QUEUE) { push @queue, [] }
    $queueCount = 0;
}

# we use AUTOLOAD to load the Event source packages, so
# Event->process will load Event::process and create a new
# sub which will call Event::process->new(@_);

sub AUTOLOAD {
    my $sub = ($Event::AUTOLOAD =~ /(\w+)$/)[0];

    eval { require "Event/" . $sub . ".pm" }
	or croak "$@, Undefined subroutine &" . $Event::AUTOLOAD;

    croak "Badly defined Event package Event::${sub}"
	unless defined &{$Event::AUTOLOAD};

    goto &{$Event::AUTOLOAD};
}

# Fake Event::idle
sub idle {
    shift;
    Event->queueIdleEvent(@_);
}

sub register {
    my $source = caller;
    $source{$source} = 1;
    my $sub = $source;
    $sub =~ s/^Event:://i;

    no strict 'refs';

    *{$sub} = sub { shift; $source->new(@_) };
}

sub registerAsync {
    my $source = caller;
    $asyncsource{$source} = 1;
    my $sub = $source;
    $sub =~ s/^Event:://i;

    no strict 'refs';

    *{$sub} = sub { shift; $source->new(@_) };
}

sub queueEvent {
    shift;
    push @{$queue[DEFAULT_QUEUE]}, @_;
    $queueCount += @_;
}

sub queueAsyncEvent {
    shift;
    push @{$queue[ASYNC_QUEUE]}, @_;
    $queueCount += @_;
}

sub queueIdleEvent {
    shift;
    push @{$queue[IDLE_QUEUE]}, @_;
    $queueCount += @_;
}

sub queuePriorityEvent {
    shift;
    my $priority = int(shift);
    $priority = 0 if $priority < 0;
    $priority = ASYNC_QUEUE if $priority > ASYNC_QUEUE;
    push @{$queue[$priority]}, @_;
    $queueCount += @_;
}

sub dispatchIdleEvents () {
    $idleCount = @{$queue[IDLE_QUEUE]};

    return 0
	unless $idleCount;

    my $cb;

    while($idleCount--) {
	$queueCount--;
	shift( @{$queue[IDLE_QUEUE]} )->();
    }

    return 1;
}

sub dispatchEvent () {
    my $q;

    # stop before the IDLE_QUEUE as we do not want to proces idle events here
    for($q = ASYNC_QUEUE ; $q > IDLE_QUEUE ; $q--) {
	if(@{$queue[$q]}) {
	    $queueCount--;
	    shift(@{$queue[$q]})->();
	    return 1;
	}
    }
    return 0;
}

sub dispatchAsyncEvents () {

    my $ret = 0;

    # loop untill we cannot find anymore async events.

    while(1) {
	map { $_->check } keys %asyncsource;

	last
	    unless @{$queue[ASYNC_QUEUE]};

	# process all that are found.

	$asyncCount = @{$queue[ASYNC_QUEUE]};

	last
	    unless $asyncCount;

	$ret = 1;

	while($asyncCount--) {
	    $queueCount--;
	    shift( @{$queue[10]} )->();
	}

    }

    $ret;
}

sub min (@) {
    return shift if @_ < 2;
    my $v = shift;
    while(@_) {
	my $a = shift;
	$v = $a if $a < $v
    }
    $v;
}
sub reduce (&@) {
    my $sub = shift;
    return shift if @_ <= 1;
    my $ret = shift;
    $ret = &{$sub}($ret,shift)
	while(@_);
    return $ret;
}

sub queuePendingEvents () {
    my $wait = $queueCount ? 0 : $maxSleep;

	# first prepare the sources, to find the max wait time

    Event::OS->PrepareSource;

    $wait = min $wait, map { $_->prepare } keys %source;

	# wait if we have to

    Event::OS->WaitForEvent($wait);

	# check for any events that happened

    map { $_->check } keys %source;

	# We do not want to hold extra references to objects
	# if we can help it.

    Event::OS->ClearSource;

    1;
}

sub DoOneEvent () {
		# abort if we have been asked to exit the loop
    return 0
	if defined($exit);

		# First check for any async events (eg signals)
    return 1
	if dispatchAsyncEvents();

		# process one queued event
    return 1
	if dispatchEvent();

		# OK, no events are queued, so check for any pending
    queuePendingEvents();

		# process one queued event
    return 1
	if dispatchEvent();

		# OK, no events were queued, or found so do the idle events
    dispatchIdleEvents();
}


sub Loop () {
    DoOneEvent
	until(defined $exit);
    my($atexit,$cb);
    while($atexit = shift @atexit) {
	$cb->()
	    if defined($cb = $$atexit);
    }
    return $exit;
}

sub exit ($) {
    shift if @_ > 1; # method call
    $exit = shift;
}

sub atexit {
    shift; # class
    require Event::atexit;
    my $obj = new Event::atexit(-callback => shift);
    push(@atexit,$obj);
    $obj;
}

1;

