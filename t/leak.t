# leak -*-perl-*-
use Test; plan test => 1, todo => [1];
use Event qw(all_events);

Event->idle()->cancel;
Event->io()->cancel;
Event->signal(signal => 'USR1')->cancel;
Event->timer(at => time)->cancel;
my $var = 1;
Event->watchvar(variable => \$var)->cancel;

my $got = join(', ', map { ref } all_events()) || 'None';
ok($got, 'None');
