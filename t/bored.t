# -*- perl -*-
use Test; plan test => 2;
use Event;

# $Event::DebugLevel = 3;

my $really_bored;
my $e;
$e = Event->timer(after => .5, callback => sub {
		      if (!$really_bored) {
			  $e->again;
			  $really_bored='yes';
		      } else {
			  ok 1;
		      }
		  });

ok !defined Event::loop();
