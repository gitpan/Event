#!./perl -w

$| = 1;
use Event;

Event->io(e_fd      => \*STDIN,
          e_timeout => 5.3,
          e_poll    => "r",
          e_repeat  => 1,
          e_cb      => sub {
	      my $e = shift;
	      my $got = $e->{e_got};
              #print scalar(localtime), " ";
	      if ($got eq "r") {
		  sysread(STDIN, $buf, 80);
		  chop $buf;
		  print "read '$buf'\n";
	      } else {
		  print "nothing for $e->{e_timeout} seconds\n";
	      }
          });

Event::loop();
