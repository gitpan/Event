# -*-perl-*- i/o

BEGIN {
    if ($^O eq 'MSWin32') {
	print "1..0\n";
	print "ok 1 # skipped; Win32 supports select() only on sockets\n";
	exit;
    }
}

use Test; plan tests => 3;
use Event qw(loop unloop);
use Event::Watcher qw(R W);
use Symbol;

#$Event::DebugLevel = 3;

sub new_pipe {
    my ($cnt) = @_;
    my ($p1,$p2) = (gensym, gensym);
    pipe($p1,$p2);

    for my $p ($p1,$p2) {
	my $io = Event->io(e_poll => 'rw', e_cb => sub {
			       my $e = shift;
			       if ($e->{e_got} & R) {
				   my $buf;
				   sysread $e->{e_fd}, $buf, 1;
				   ++$$cnt;
			       }
			       if ($e->{e_got} & W) {
				   syswrite $e->{e_fd}, '.', 1;
			       }
			   }, e_desc => "pair $p");
	$io->{e_fd} = $p;
    }
}

my $count = 0;
new_pipe(\$count);

my $hit=0;
my $once = Event->io(e_timeout => .01, e_cb => sub { ++$hit });

Event->io(e_timeout => .1, e_cb => sub {
	      ok $count > 0;
	      ok $hit, 1;
	      ok $once->{e_timeout}, 0;
	      unloop;
	  });

loop();
