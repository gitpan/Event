# the time for -*-perl-*-

use Test; plan tests => 6;
use Event qw(loop unloop);

#$Event::DebugLevel = 2;

my $count = 0;
Event->timer(after => 0.5, interval => .1, nice => -1,
	     callback => sub { ++$count }, desc => "counter");

my $when = time + 2;
Event->timer(at => $when, callback => sub { ok $when, $_[0]->{at}; },
	     desc => "at");

my $again;
Event->timer(after => .5, callback => sub {
		 my $o=shift;
		 ok 1;
		 if (!$again) {
		     $again=1;
		     $o->again;
		     $o->again;  #should be harmless
		 }
	     }, desc => "after");

my $ok = Event->timer(interval => .5, callback => sub {
			  unloop('ok') if $count > 30
		      }, desc => "exit");
ok abs($ok->{at} - time) < 3, 1, "diff was ".($ok->{at} - time);

my $long;
for (1..10) {
    $long = Event->timer(after => 60+rand(60), callback => sub { ok 0; });
}
$long->{callback} = sub { ok 1 };
$long->{at} = time;

ok loop(), 'ok';
