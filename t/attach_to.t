#!./perl -w

use strict;
use Test; plan tests => 2;
use Event 0.53;

my $array = Event->timer(attach_to => [0,1,2], after => 1);
ok $array->[2], 2;
ok $array->interval, 1;
