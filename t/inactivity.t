# -*-perl-*- screensaver

use strict;
use Test; plan test => 6;
use Event qw(loop);

# $Event::DebugLevel = 3;

my $hit=0;
my $ss = Event->inactivity(e_interval => .1, e_cb => sub { ++$hit });
ok !$ss->{e_suspend};
$ss->suspend;
ok $ss->{e_suspend};

ok !Event::queue_time(4);
my $on;
my $tm = Event->timer(e_interval => .01, e_cb => sub {
			  if (!$on) {
			      ok Event::queue_time(4);
			      $ss->{e_suspend}=0;
			      $on=1;
			  }
		      });
loop(.5);
ok $hit, 0;  # somehow this fails sometimes!  i don't get it!

$tm->{e_prio} = 5;
loop(1);  # I'm not sure why there is so much delay. XXX
ok $hit;
