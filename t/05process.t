# process this -*-perl-*- !!

use Test;
BEGIN { plan tests => 4 }
use Event;

# $Event::DebugLevel = 3;

my $child = 0;

Event->idle(
    callback => sub {
	ok 1;

	unless($child = fork) {
	    sleep(1);
	    exit(0);
	}
    }
);

Event->process(
    callback =>
	sub {
	    my($cb,$pid,$status) = @_;

	    ok !$status;
	    ok $pid, $child;

	    Event->exit
	},
);

Event->Loop;

ok 1;

