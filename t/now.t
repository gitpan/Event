# -*-perl-*- ASAP

use Test; plan tests => 3;
use Event qw(loop unloop);
use Event::type qw(idle timer);

# $Event::DebugLevel = 3;

my $c=0;
Event->idle(repeat => 1, cb => sub { 
		++$c;
		unloop if $c >= 2;
	    })
    ->now;
my $e = Event->timer(after => 10, cb => sub { ok 1 });
$e->stop;
$e->now;

loop;
ok $c, 2;
ok $e->cbtime;
