# -*-perl-*- ASAP

use Test; plan tests => 1;
use Event qw(loop unloop);

# $Event::DebugLevel = 3;

my $c=0;
Event->idle(repeat => 1, callback => sub { 
		++$c;
		unloop if $c >= 2;
	    })
    ->now;

loop;
ok $c, 2;
