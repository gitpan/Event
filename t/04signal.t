# signalling all -*-perl-*- programmers...

use Test;
BEGIN { plan tests => 4 }
use Event;

#$Event::DebugLevel = 3;

my $count = 3;

Event->signal(
    signal => 'USR1',
    callback =>
	sub {
	    my($cb, $sig, $count) = @_;

	    ok $sig, 'USR1';
	    ok $count, 2;

	    Event->exit
	}
);

my $idle;
$idle = Event->idle(
    callback => sub {
	kill 'USR1',$$;
	kill 'USR1',$$;
	ok 1;
    }
);

Event->Loop;

ok 1;
