# -*-perl-*- i/o

use Test; plan tests => 2;
use Event qw(loop unloop);
use Symbol;

# $Event::DebugLevel = 1;

sub new_pipe {
    my ($cnt) = @_;
    my ($p1,$p2) = (gensym, gensym);
    pipe($p1,$p2);

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
		 unloop if $count > 50;
	     });

loop();

ok 1;
