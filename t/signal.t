# signal -*-perl-*-

use Test; plan tests => 4;
use Event qw(loop unloop);

#$Event::DebugLevel = 3;

my $count = 3;

Event->signal(
    signal => 'USR1',
    callback =>
	sub {
	    my $e = shift;

	    ok $e->{signal}, 'USR1';
	    ok $e->{count}, 2;

	    unloop;
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

loop;

ok 1;
