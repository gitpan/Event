#!./perl -w

use strict;
use Test; plan test => 7;
use Event;

eval { Event->io };
ok $@, '/unconfigured/';

eval { Event->signal };
ok $@, '/no signal/';

eval { Event->timer };
ok $@, '/unset/';

eval { Event->timer(parked => 1, interval => -1)->again };
ok $@, '/non\-positive/';

eval { Event->var };
ok $@, '/watching what/';

my $var = 1;

eval { Event->var(poll => 0, var => \$var) };
ok $@, '/no poll events/';

eval { Event->var(var => \$1) };
ok $@, '/read\-only/';
