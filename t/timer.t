# the time for -*-perl-*-

use Test; plan tests => 6;
use Event qw(loop unloop);

#$Event::DebugLevel = 2;

my $count = 0;
Event->timer(e_after => 0.5, e_interval => .1, e_nice => -1,
	     e_cb => sub { ++$count }, e_desc => "counter");

my $when = time + 2;
Event->timer(e_at => $when, e_cb => sub { ok $when, $_[0]->{e_at}; },
	     e_desc => "at");

my $again;
Event->timer(e_after => .5, e_cb => sub {
		 my $o=shift;
		 ok 1;
		 if (!$again) {
		     $again=1;
		     $o->again;
		     $o->again;  #should be harmless
		 }
	     }, e_desc => "after");

my $ok = Event->timer(e_interval => .5, e_cb => sub {
			  unloop('ok') if $count > 30
		      }, e_desc => "exit");
ok abs($ok->{e_at} - time) < 3, 1, "diff was ".($ok->{e_at} - time);

my $long;
for (1..10) {
    $long = Event->timer(e_after => 60+rand(60), e_cb => sub { ok 0; });
}
$long->{e_cb} = sub { ok 1 };
$long->{e_at} = time;

ok loop(), 'ok';
