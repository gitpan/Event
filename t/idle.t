# idle daydreams of -*-perl-*-

use Test;
BEGIN { plan tests => 6 }

use Event;
ok 1;

$Event::Eval = 1;
#$Event::DebugLevel = 3;

package myobj;
use Test;

my $myobj;
sub idle {
    my ($o,$e) = @_;
    if (!$myobj) {
	ok $o, 'myobj';
	ok $e->isa('Event');
	ok $e->{desc}, __PACKAGE__;
    }
    ++$myobj;
}
Event->idle(callback => [__PACKAGE__,'idle'],
	    desc => __PACKAGE__);

package main;

my $count=0;
my $idle = Event->idle(callback =>
		       sub {
			   my $e = shift;
			   ++$count;
			   if ($count > 2 && $myobj) {
			       Event->exit;
			   } else {
			       #$e->again;
			   }
		       },
		       desc => "exit");

ok ref($idle), 'Event::idle';

Event->idle(callback => sub { ok 0; Event->exit })
    ->cancel;

Event->idle(callback => sub { $idle->again },
	    repeat => 1,
	    desc => "again");

Event->Loop;

ok 1;

