# -*-perl-*- ASAP

use Test; plan tests => 2;
use Event qw(loop unloop);

# $Event::DebugLevel = 3;

my $c=0;
Event->idle(repeat => 1, callback => sub { 
		++$c;
		unloop if $c >= 2;
	    })
    ->now;
my $e = Event->timer(after => 10, callback => sub { ok 1 });
$e->cancel;
$e->now;

loop;
ok $c, 2;
