# hook -*-perl-*-

use strict;
use Test; plan test => 3;
use Event qw(sweep);

my ($p,$c,$ac) = (0)x3;

Event::add_hooks(prepare => sub { ++$p },
		 check => sub { ++$c },
		 asynccheck => sub { ++$ac });

sweep();

ok $p,1;
ok $c,1;
ok $ac,1;
