# -*-perl-*- has great timing

use Test;
BEGIN { plan tests => 5 }
use Event;

# $Event::DebugLevel = 3;

my $count = 0;

Event->timer(
    callback => sub { ok 1 },
    after    => 1
);

my $when = time + 2;

Event->timer(
    callback =>
	sub {
	    my($cb,$time) = @_;
	    
	    ok $time, $when;

	    Event->timer(
		callback =>
		    sub {
			ok 1;
	        	Event->exit if ++$count == 2;
		    },
		after    => 0.5,
		interval => .2
	    );
	},
    at => $when
);


Event->Loop;

ok 1;
