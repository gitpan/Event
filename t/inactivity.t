# -*-perl-*- screensaver

use strict;
use Test; plan test => 2;
use Event qw(loop);

#$Event::DebugLevel = 2;

my $hit=0;
my $ss = Event->inactivity(interval => .05,
			   callback => sub { ++$hit });;
$ss->suspend;

my $tm = Event->timer(interval => .01, callback => sub {
			  $ss->resume;
		      });
loop(.25);
ok $hit, 0;

$tm->{priority} = 5;
loop(1);  # I'm not sure why there is so much delay. XXX
ok $hit;
