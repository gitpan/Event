# idle -*-perl-*- thoughts...

use Test;
BEGIN { plan tests => 5 }

use Event;
ok 1;

package myobj;
use Test;

my $myobj;
sub idle {
    my ($o,$e) = @_;
    if (!$myobj) {
	ok $o, 'myobj';
	ok $e->isa('Event');
    }
    ++$myobj;
}
Event->idle(callback => ['myobj','idle'], repeat => 1);

package main;

#$Event::DebugLevel = 1;

my $count=0;
my $idle = Event->idle(callback =>
		       sub {
			   my $e = shift;
			   ++$count;
			   if ($count > 2 && $myobj) {
			       Event->exit;
			   } else {
			       $e->again;
			   }
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

