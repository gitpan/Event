# -*-perl-*- i/o

BEGIN {
    if ($^O eq 'MSWin32') {
	print "1..0\n";
	print "ok 1 # skipped; Win32 supports select() only on sockets\n";
	exit;
    }
}

use Test; plan tests => 2;
use Event qw(loop unloop);
BEGIN { Event::Watcher->import(qw(R W)) }
use Symbol;

# $Event::DebugLevel = 1;

sub new_pipe {
    my ($cnt) = @_;
    my ($p1,$p2) = (gensym, gensym);
    pipe($p1,$p2);

    for my $p ($p1,$p2) {
	Event->io(e_fd => $p, e_poll => 'rw', e_cb => sub {
		      my $e = shift;
		      if ($e->{e_got} & R) {
			  my $buf;
			  sysread $p, $buf, 1;
			  ++$$cnt;
		      }
		      if ($e->{e_got} & W) {
			  syswrite $p, ".", 1;
		      }
	      }, e_desc => "pair $p");
    }
}

my $count = 0;
new_pipe(\$count);
ok 1;

Event->timer(e_interval => .5, e_cb => sub {
		 unloop if $count > 50;
	     });

loop();

ok 1;
