use Event;

$w=Event->io(fd=>\*STDIN, cb=>sub {<STDIN>; warn "CALLED!\n";});

print "io default poll attribute:", $w->poll, "\n";

Event::loop();
