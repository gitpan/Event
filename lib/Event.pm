use strict;

BEGIN {  # do the right thing for threads?
    eval { require attrs; } or do {
	$INC{'attrs.pm'} = "";
	*attrs::import = sub {};
    }
}

package Event;
use base 'Exporter';
use Carp;
use vars qw($VERSION @EXPORT_OK
	    $API $DebugLevel $Eval $DIED $Now);
use vars qw(%KEY_REMAP);
$VERSION = '0.32';

# If we inherit DynaLoader then we inherit AutoLoader; Bletch!
require DynaLoader;

# DynaLoader calls dl_load_flags as a static method.
*dl_load_flags = DynaLoader->can('dl_load_flags');
(defined(&bootstrap)? \&bootstrap : \&DynaLoader::bootstrap)->
    (__PACKAGE__, $VERSION);

# Try to load Time::HiRes
eval { require Time::HiRes; };
die if $@ && $@ !~ /^Can\'t locate .*? at \(eval /;

install_time_api();  # broadcast_adjust XXX

$DebugLevel = 0;
$Eval = 0;		# should avoid because c_callback is exempt
$DIED = \&default_exception_handler;

@EXPORT_OK = qw(time all_events all_watchers all_running all_queued all_idle
		one_event sweep loop unloop unloop_all sleep queue
		QUEUES PRIO_NORMAL PRIO_HIGH
		%KEY_REMAP);

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
    my $desc = $run? $run->{e_desc} : '?';
    my $m = "Event: trapped error in '$desc': $err";
    $m .= "\n" if $m !~ m/\n$/;
    warn $m;
    #Carp::cluck "Event: fatal error trapped in '$desc'";
}

sub verbose_exception_handler { #AUTOLOAD XXX
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

my $loop_timer;
sub loop {
    use integer;
    if (@_ == 1) {
	my $how_long = shift;
	if (!$loop_timer) {
	    $loop_timer = Event->timer(e_desc => "Event::loop timeout",
				       e_after => $how_long,
				       e_cb => sub { unloop($how_long) });
	    $loop_timer->{e_prio} = PRIO_HIGH();
	} else {
	    $loop_timer->{e_at} = Event::time() + $how_long,
	}
	$loop_timer->start;
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
    $loop_timer->stop if $loop_timer;
    warn "Event: [$LoopLevel]unloop(".(defined $Result?$Result:'<undef>').")\n"
	if $Event::DebugLevel >= 3;
    $Result;
}

sub unloop {
    $Result = shift;
    --$ExitLevel;
}

sub unloop_all {
    $Result = shift; #propagate result somehow? XXX
    $ExitLevel = 0;
}

sub add_hooks {
    shift if @_ & 1; #?
    while (@_) {
	my $k = shift;
	my $v = shift;
	croak "$v must be CODE" if ref $v ne 'CODE';
	_add_hook($k, $v);
    }
}

END { $_->cancel for all_watchers() }

require Event::Watcher;

#----------------------------------- backward compatibility
#----------------------------------- backward forward backward

my $backward_noise = 20;

if (1) {
    # Do you feel like you need entwash?  Have some of this!
    no strict 'refs';

    # 0.25
    %KEY_REMAP = (after		 => 'e_after',
		  async		 => 'e_async',
		  at             => 'e_at',
		  callback       => 'e_cb',
		  cbtime         => 'e_cbtime',
		  clump          => 'e_clump',
		  count          => 'e_hits',
		  debug          => 'e_debug',
		  desc           => 'e_desc',
		  events         => 'e_poll',
		  flags          => 'e_flags',
		  got            => 'e_got',
		  handle         => 'e_fd',
		  hard           => 'e_hard',
		  id             => 'e_id',
		  interval       => 'e_interval',
		  level		 => 'e_level',
		  max_interval   => 'e_max',
		  min_interval   => 'e_min',
		  nice		 => 'e_nice',
		  priority       => 'e_prio',
		  reeentrant     => 'e_reentrant',
		  refcnt         => 'e_refcnt',
		  repeat         => 'e_repeat',
		  running        => 'e_running',
		  signal         => 'e_signal',
		  timeout        => 'e_timeout',
		  variable       => 'e_var');
}

1;
