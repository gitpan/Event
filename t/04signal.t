
use Event;

print "1..4\n";

my $count = 3;

Event->signal(
    -signal => 'INT',
    -callback =>
	sub {
	    my($cb, $sig) = @_;

	    print "ok 2\n";
	    
	    print "not " unless $sig eq "INT";
	    print "ok 3\n";

	    Event->exit
	}
);

Event->idle(
    sub {
	print "ok 1\n";
	kill 'INT',$$
    },
);

Event->Loop;

print "ok 4\n";
