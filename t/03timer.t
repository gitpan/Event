
use Event;

print "1..6\n";

my $count = 0;

Event->timer(
    -callback => sub { print "ok 1\n"; },
    -after    => 1
);

my $when = time + 2;

Event->timer(
    -callback =>
	sub {
	    my($cb,$time) = @_;
	    
	    print "ok 2\n";

	    print "not " unless $time == $when;
	    print "ok 3\n";

	    Event->timer(
		-callback =>
		    sub {
			print "ok ",4 + $count++,"\n";
	        	Event->exit if $count == 2;
		    },
		-after    => 0.5,
		-interval => 0.2
	    );
	},
    -at => $when
);


Event->Loop;

print "ok 6\n";
