#!./perl -w

use strict;
use Test; plan tests => 3;
use Event 0.65;

my $invoked_method=0;
sub method {
    ++$invoked_method;
}
my $main = bless [];

Event->timer(after => 0, cb => \&method);
Event->timer(after => 0, cb => ['main', 'method']);
Event->timer(after => 0, cb => [$main, 'method']);
{
    local $SIG{__WARN__} = sub {
	ok $_[0], '/nomethod/';
    };
    Event->timer(after => 0, cb => [$main, 'nomethod'])->cancel;
}

eval { Event->timer(after => 0, cb => ['main']); };
ok $@, '/Callback/';

Event::loop();
ok $invoked_method, 3;
