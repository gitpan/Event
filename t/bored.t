# -*- perl -*-
use Test; plan test => 4;
use Event;

# $Event::DebugLevel = 3;

my $really_bored;
my $e;
$e = Event->timer(after => .5);
ok !defined $e->cb;
$e->cb(sub {
	   if (!$really_bored) {
	       $e->again;
	       $really_bored='yes';
	   } else {
	       ok 1;
	   }
       });
ok ref $e->cb, 'CODE';

ok !defined Event::loop();
