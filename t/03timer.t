# -*-perl-*- has great timing

use Test;
BEGIN { plan tests => 6 }
use Event;

# $Event::DebugLevel = 3;

my $count = 0;
Event->timer(after => 0.5, interval => .1, priority => -1,
	     callback => sub { ++$count });

my $when = time + 2;
Event->timer(at => $when, callback => sub { ok $when, $_[1]; });

my $again;
my $timer;
$timer = Event->timer(after => 1, callback => sub {
		 ok 1;
		 if (!$again) {
		     $again=1;
		     $timer->again;
		     $timer->again;  #should be harmless
		 }
	     });
Event->timer(interval => .5, callback => sub {
		 Event::Loop::exitLoop('ok') if $count > 30
	     });

ok Event::Loop::Loop(), 'ok';
