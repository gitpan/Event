
use Event;

$| = 1;

print "1..5\n";

my $count = 3;
my $child = 0;

Event->idle(
    sub {
	local *F;
	print "ok 1\n";
	$child = open(F,"|-") || exit(0);
    }
);

Event->process(
    -callback =>
	sub {
	    my($cb,$pid,$status) = @_;
	    print "ok 2\n";

	    print "not " if($status);
	    print "ok 3\n";

	    print "not " unless($pid == $child);
	    print "ok 4\n";

	    Event->exit;
	},
);

Event->Loop;

print "ok 5\n";
