#!./perl -w

use strict;
use Config;
use Event 0.08;
use Time::HiRes qw(time);
use vars qw($VERSION $TestTime);
$VERSION = '0.04';
$TestTime = 11;

# $Event::DebugLevel = 3;

Event->timer(callback => sub { Event->exit }, after => $TestTime);

#------------------------------ Timer
use vars qw($TimerCount $TimerExpect);
$TimerCount = 0;
$TimerExpect = 0;
for (1..20) {
    my $interval = .2 + .1 * int rand 3;
    Event->timer(callback => sub { ++$TimerCount },
		 interval => $interval);
    $TimerExpect += $TestTime/$interval;
}

#------------------------------ Signals
use vars qw($SignalCount);
$SignalCount = 0;
Event->signal(signal => 'USR1',
	      callback => sub { ++$SignalCount; });
Event->timer(callback => sub { kill 'USR1', $$; },
	     interval => .5);

#------------------------------ IO
use vars qw($IOCount @W);
$IOCount = 0;

use Symbol;
for (1..15) {
    my ($r,$w) = (gensym,gensym);
    pipe($r,$w);
    select $w;
    $|=1; 
    Event->io(handle => $r,
	      callback => sub {
		  my $buf;
		  ++$IOCount;
		  sysread $r, $buf, 1;
	      },
	      events => 'r');
    push @W, $w;
}
select STDOUT;

#------------------------------ Idle
use vars qw($IdleCount);
$IdleCount = 0;

my $idle;
$idle = Event->idle(callback => sub {
    ++$IdleCount;
    for (0..@W) {
	my $w = $W[int rand @W];
	syswrite $w, '.', 1;
    }
    $idle->again;
});

#------------------------------ Loop

sub run {
    my $start = time;
    Event->Loop;
    time - $start;
}
my $elapse = &run;

sub pct { 
    my ($got, $expect) = @_;
    sprintf "%.2f%%", 100*$got/$expect;
}

warn "Timing a null loop...\n";
my $null = Event::Loop::null_loops_per_second(7);

chomp(my $uname = `uname -a`);
print "
 benchmark: $VERSION
 Time::HiRes: $Time::HiRes::VERSION, Event: $Event::VERSION

 perl $]
 uname=$uname
 cc='$Config{cc}', optimize='$Config{optimize}'
 ccflags='$Config{ccflags}'

 Please mail benchmark results to perl-loop\@perl.org.  Thanks!

Elapse Time:     ".pct($elapse,$TestTime)." of $TestTime seconds
Timer/sec:       ".pct($TimerCount,$TimerExpect)." ($TimerCount total)
Io/sec:          ".sprintf("%.3f", $IOCount/$elapse)." ($IOCount total)
Signals/sec      ".sprintf("%.2f", $SignalCount/$elapse)."
Events/sec       ".sprintf("%.3f", ($IdleCount+$TimerCount+$IOCount+$SignalCount)/$elapse)."
Null/sec         $null

";

