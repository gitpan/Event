#!./perl -w

use Event;
use IO::Socket;

$Event::DebugLevel = 3;

$s = IO::Socket::INET->new(Listen=>5, LocalPort => 5678);
Event->io(fd => $s, poll => "r", cb => sub {
	      my @c = Event::_memory_counters();
	      warn join(' ', @c);
	  });
Event::loop();

#  349 joshua   -15    0 4504K 4256K sleep   0:00  2.53%  2.53% perl
