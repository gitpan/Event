# process -*-perl-*-

BEGIN {
    print "1..0\n";
    print "ok 1 # skipped; This is completely broken!\n";
    exit;
    if ($^O eq 'MSWin32') {
	print "1..0\n";
	print "ok 1 # skipped; Win32 doesn't support fork()\n";
	exit;
    }
}

use strict;
use Test; plan tests => 6;
use Event qw(loop unloop);

# If this test doesn't terminate, try uncommenting the following line
# and post the output of a test run to the perl-loop mailing list.
# Thanks!

# $Event::DebugLevel = 4;

sub myexit {
    my $st = shift;
    warn "[PID $$ exiting with $st]\n"
	if $Event::DebugLevel;
    exit $st;
}

my $got=0;
sub maybe_done { unloop if ++$got >= 2 }

my $child1;
unless($child1 = fork) { sleep 2; myexit 5 }
Event->process(callback => sub {
		   my $o = shift;
		   ok $o->{status}, 5 << 8; #unix only XXX?
		   ok $o->{pid}, $child1;

		   maybe_done();
	       });

my $child2;
unless ($child2 = fork) { sleep 10000; myexit 6 }
Event->process(pid => $child2, timeout => 2.5, callback => sub {
		   my $o = shift;
		   if (!exists $o->{status}) {
		       warn "[killing $o->{pid}]\n" if $Event::DebugLevel;
		       ok 1;
		       kill 9, $o->{pid};  # 9 always kill? XXX
		       return;
		   }
		   ok $o->{status}, 9;
		   ok $o->{pid} == $child2;

		   maybe_done();
	       });

warn "[entering loop]\n" if $Event::DebugLevel;
loop;
ok 1;

# try repeating? XXX
