# -*-perl-*- i/o

use Test; plan tests => 2;
use Event;
use Symbol;
# use IO::Handle;

$Event::DebugLevel = 1;

sub new_pipe {
    my ($cnt) = @_;
    my ($p1,$p2) = (gensym, gensym);
    pipe($p1,$p2);

#    $p2 = IO::Handle->new_from_fd(fileno($p2), 'r');

    for my $p ($p1,$p2) {
	Event->io(handle => $p, events => 'rw', callback => sub {
		      my $e = shift;
		      if ($e->{got} =~ /r/) {
			  my $buf;
			  sysread $p, $buf, 1;
			  ++$$cnt;
		      }
		      if ($e->{got} =~ /w/) {
			  syswrite $p, ".", 1;
		      }
	      }, desc => "pair $p");
    }
}

my $count = 0;
new_pipe(\$count);
ok 1;

Event->timer(interval => .5, callback => sub {
		 Event->exit if $count > 50;
	     });

Event::Loop::Loop();

ok 1;
