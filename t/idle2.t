# idle daydreams -*-perl-*-

use Test; plan tests => 5;
use Event qw(loop unloop time all_events one_event);
require Event::timer;

# $Event::Eval = 1;
# $Event::DebugLevel = 4;
$Event::DIED = \&Event::verbose_exception_handler;

#----------- complex idle events; fuzzy timers

my ($cnt,$min,$max,$sum) = (0)x4;
my $prev;
$min = 100;
my $Min = .01;
my $Max = .2;
Event->idle(e_min => $Min, e_max => $Max, e_desc => "*IDLE*TEST*",
	    e_cb => sub {
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
Event->idle(e_repeat => 1, e_cb => sub { Event::sleep $Min; ++$sleeps });

Event::sleep .1; # try to let CPU settle
loop();

my $epsilon = .05;
ok $sleeps > 1; #did we test anything?
ok $min >= $Min-$epsilon;
ok $max < $Max+$epsilon;
ok $sum/$cnt >= $min;
ok $sum/$cnt <= $max;