# signal -*-perl-*-

BEGIN {
    if ($^O eq 'MSWin32') {
	print "1..0\n";
	print "ok 1 # skipped; kill() doesn't send signals on Win32\n";
	exit;
    }
}

use Test; plan tests => 4;
use Event qw(loop unloop);

#$Event::DebugLevel = 3;

my $count = 3;

Event->signal(
    e_signal => 'USR1',
    e_cb =>
	sub {
	    my $e = shift;

	    ok $e->{e_signal}, 'USR1';
	    ok $e->{e_hits}, 2;

	    unloop;
	}
);

my $idle;
$idle = Event->idle(
    e_cb => sub {
	kill 'USR1',$$;
	kill 'USR1',$$;
	ok 1;
    }
);

loop;

ok 1;
