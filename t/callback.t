#!./perl -w

use strict;
use Test; plan tests => 3;
use Event 0.53;
use Event::type qw(timer);

my $invoked_method=0;
sub method {
    ++$invoked_method;
}
my $main = bless [];

Event->timer(after => 0, cb => \&method);
Event->timer(after => 0, cb => ['main', 'method']);
Event->timer(after => 0, cb => [$main, 'method']);

eval { Event->timer(after => 0, cb => ['main']); };
ok $@, '/Callback/';
eval { Event->timer(after => 0, cb => ['main', \&method]); };
ok $@, '/Callback/';

Event::loop();
ok $invoked_method, 3;
