# the time for -*-perl-*-

use Test; plan tests => 6;
use Event qw(loop unloop);
require Event::timer;

# $Event::DebugLevel = 4;

my $count = 0;
Event->timer(after => 0.5, interval => .1, nice => -1,
	     cb => sub { ++$count }, desc => "counter");

my $when = time + 2;
Event->timer(at => $when, cb => sub { ok $when, $_[0]->w->at; },
	     desc => "at");

my $again;
Event->timer(after => .5, cb => sub {
		 my $o=shift;
		 ok 1;
		 if (!$again) {
		     $again=1;
		     $o->w->again;
		     $o->w->again;  #should be harmless
		 }
	     }, desc => "after");

my $ok = Event->timer(interval => .5, cb => sub {
			  unloop('ok') if $count > 30
		      }, desc => "exit");
ok abs($ok->at - time) < 3, 1, "diff was ".($ok->at - time);

my $long;
for (1..10) {
    $long = Event->timer(after => 60+rand(60), cb => sub { ok 0; });
}
$long->cb(sub { ok 1 });
$long->at(time);

ok loop(), 'ok';
