
use Event;

print "1..2\n";

Event->idle(
    sub {
	print "ok 1\n";
	Event->exit
    }
);

Event->Loop;

print "ok 2\n";
