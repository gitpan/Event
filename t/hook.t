# hook -*-perl-*-

use strict;
use Test; plan test => 4;
use Event qw(sweep);

my ($p,$c,$ac,$cb) = (0)x3;

Event::add_hooks(prepare => sub { ++$p },
		 check => sub { ++$c },
		 asynccheck => sub { ++$ac },
		 callback => sub { ++$cb });
Event->timer(e_after => 0, e_cb => sub {});

sweep();

ok $p,1;
ok $c,1;
ok $ac,1;
ok $cb,1;
