#!./perl -w

$| = 1;
use Event;
require Event::io;

Event->io(fd      => \*STDIN,
          timeout => 2.5,
          poll    => "r",
          repeat  => 1,
          cb      => sub {
	      my $e = shift;
	      my $got = $e->got;
              #print scalar(localtime), " ";
	      if ($got eq "r") {
		  sysread(STDIN, $buf, 80);
		  chop $buf;
		  my $len = length($buf);
		  Event::unloop if !$len;
		  print "read[$len]:$buf:\n";
	      } else {
		  print "nothing for ".$e->w->timeout." seconds\n";
	      }
          });

Event::loop();
