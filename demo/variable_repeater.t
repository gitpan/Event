#!./perl -w
# contributed by jsalmon@gw.thesalmons.org

use Event qw(loop);

$w = Event->timer(interval => 1);
$w->cb(sub {
	   my $next = rand(10);
	   print(scalar localtime(Event::time()), ": waiting ",
		 $next, " sec\n");
	   $w->interval($next);
       });

loop();
