#!./perl -w

use strict;
use Test; plan tests => 1;
use Event 0.30 qw(loop unloop);

package Foo;

my $foo=0;
sub DESTROY { ++$foo }

package main;

my $e = Event->timer(e_after => 0,
		     e_cb => sub { delete shift->{foo}; unloop });
$e->{foo} = bless [], 'Foo';

loop();
ok $foo;
