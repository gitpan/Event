#!./perl -w

use Test; plan tests => 6;
use Event;

my $w = Event->idle(parked => 1);

ok !$w->data;
ok $w->data(1);
ok $w->data;

package Grapes;
use Test;

ok !$w->data;
ok $w->data(2), 2;

package main;

ok $w->data, 1;
