# leak -*-perl-*-
use Test; plan test => 1, todo => [1];
use Event;

Event->idle()->cancel;
Event->io()->cancel;
Event->signal(signal => 'USR1')->cancel;
Event->timer(at => time)->cancel;
my $var = 1;
Event->watchvar(variable => \$var)->cancel;

my $got = join(', ', map { ref } Event::Loop::events()) || 'None';
ok($got, 'None');
