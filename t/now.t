# -*-perl-*- ASAP

use Test; plan tests => 2;
use Event qw(loop unloop);

# $Event::DebugLevel = 3;

my $c=0;
Event->idle(e_repeat => 1, e_cb => sub { 
		++$c;
		unloop if $c >= 2;
	    })
    ->now;
my $e = Event->timer(e_after => 10, e_cb => sub { ok 1 });
$e->stop;
$e->now;

loop;
ok $c, 2;
