# -*-perl-*- screensaver

use strict;
use Test; plan test => 2;
use Event qw(loop);

# $Event::DebugLevel = 2;

my $hit=0;
my $ss = Event->inactive(interval => .105, callback => sub { ++$hit });
my $tm = Event->timer(interval => .01, callback => sub {});
loop(.25);
ok $hit, 0;

$tm->{priority} = 5;
loop(.25);
ok $hit, 2;
