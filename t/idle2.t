# idle daydreams of -*-perl-*-

use Test; plan tests => 5;
use Event qw(loop unloop time all_events one_event);
require Event::timer;

# $Event::Eval = 1;
# $Event::DebugLevel = 2;
$Event::DIED = \&Event::verbose_exception_handler;

#----------- complex idle events; fuzzy timers

my ($cnt,$min,$max,$sum) = (0)x4;
my $prev;
$min = 100;
my $Min = .01;
my $Max = .2;
Event->idle(min_interval => $Min, max_interval => $Max, desc => "*IDLE*TEST*",
	    callback => sub {
		my $now = time;
		if (!$prev) { $prev = time; return }
		my $d = $now - $prev;
		$prev = $now;
		$sum += $d;
		$min = $d if $d < $min;
		$max = $d if $d > $max;
		unloop('done') if ++$cnt > 10;
	    });
my $sleeps=0;
Event->idle(repeat => 1, callback => sub { Event::sleep $Min; ++$sleeps });

loop();

my $epsilon = .025;
ok $sleeps > 1; #did we test anything?
ok $min >= $Min-$epsilon;
ok $max < $Max+$epsilon;
ok $sum/$cnt >= $min;
ok $sum/$cnt <= $max;
