# idle -*-perl-*- thoughts...

use Test;
BEGIN { plan tests => 5 }

use Event;
ok 1;

#$Event::DebugLevel = 1;

my $count=0;
my $idle = Event->idle(callback =>
		       sub {
			   ++$count;
			   ok 1;
			   Event->exit if $count == 2;
		       });

Event->idle(
    callback => sub {
	ok 0;
	Event->exit
    }
)->cancel;

Event->idle(
    callback => sub {
	$idle->again;
	ok 1;
    }
);

Event->Loop;

ok 1;

