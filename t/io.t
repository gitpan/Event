# -*-perl-*- i/o

BEGIN {
    if ($^O eq 'MSWin32') {
	print "1..0\n";
	print "ok 1 # skipped; Win32 supports select() only on sockets\n";
	exit;
    }
}

use Test; plan tests => 4;
use Event qw(loop unloop);
use Event::Watcher qw(R W);
use Symbol;

#$Event::DebugLevel = 3;

my $noticed_bogus_fd=0;
Event->io(desc => 'oops', poll => 'r', fd => 123, cb => sub { warn 'oops' });

$SIG{__WARN__} = sub {
    my $is_it = $_[0] =~ m/\'oops\' was unexpectedly/;
    if ($is_it) {
	++$noticed_bogus_fd;
    } else {
	warn $_[0]
    }
};

sub new_pipe {
    my ($cnt) = @_;
    my ($p1,$p2) = (gensym, gensym);
    pipe($p1,$p2);

    for my $p ($p1,$p2) {
	my $io = Event->io(poll => 'rw', cb => sub {
			       my $e = shift;
			       my $w=$e->w;
			       if ($e->got & R) {
				   my $buf;
				   sysread $w->fd, $buf, 1;
				   ++$$cnt;
			       }
			       if ($e->got & W) {
				   syswrite $w->fd, '.', 1;
			       }
			   }, desc => "pair $p");
	$io->fd($p);
    }
}

my $count = 0;
new_pipe(\$count);

my $hit=0;
my $once = Event->io(timeout => .01, repeat => 0, cb => sub { ++$hit });

Event->io(timeout => .1, repeat => 0,
	  cb => sub {
	      ok $count > 0;
	      ok $hit, 1;
	      ok $once->timeout, 0;
	      unloop;
	  });

loop();

ok $noticed_bogus_fd, 1;
