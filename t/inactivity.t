# -*-perl-*- screensaver

use strict;
use Test; plan test => 4;
use Event qw(loop);

#$Event::DebugLevel = 2;

my $hit=0;
my $ss = Event->inactivity(e_interval => .05, e_cb => sub { ++$hit });
$ss->suspend;

ok !Event::queue_time(4);
my $on;
my $tm = Event->timer(e_interval => .01, e_cb => sub {
			  if (!$on) {
			      ok Event::queue_time(4);
			      $ss->resume;
			      $on=1;
			  }
		      });
loop(.5);
ok $hit, 0;

$tm->{e_prio} = 5;
loop(1);  # I'm not sure why there is so much delay. XXX
ok $hit;
