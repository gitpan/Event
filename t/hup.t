#!./perl -w

# contributed by Gisle Aas <aas@gaustad.sys.sol.no>

use Event qw(loop unloop);
use Test; plan test => 1;

$| = 1;
my $pid = open(PIPE, "-|");
die unless defined $pid;
unless ($pid) {
    # child
    for (1..100) { print "."; }
    print "\n";
    exit;
}

my $bytes = 0;
Event->io(poll => "r",
          fd   => \*PIPE,
          cb   => sub {
             my $e = shift;
	     my $buf;
             my $n = sysread(PIPE, $buf, 10);
             $bytes += $n;
             #print "Got $n bytes\n";
             unloop() unless $n;
          });

loop();

ok $bytes, 101;
