#!./perl -w

$| = 1;
use Event;
Event->io(-handle => \*STDIN,
          -timeout => 1.5,
          -events => "r",
          -callback => sub {
	      my $e = shift;
	      my $got = $e->{got};
	      if ($got eq "r") {
		  sysread(STDIN, $buf, 80);
		  chop $buf;
		  print "read '$buf'\n";
	      } else {
		  print "nothing for $e->{timeout} seconds\n";
	      }
          });

Event::loop();
