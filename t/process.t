# process -*-perl-*-

use Test; plan tests => 5;
use Event qw(loop unloop);

# If this test doesn't terminate, try uncommenting the following lines
# and post the output of a test runtime to the perl-loop mailing list.
# Thanks!

#$Event::Eval = 1;
#$Event::DebugLevel = 2;

my $sleep=2;

sub myexit {
    my $st = shift;
    warn "[PID $$ exiting with $st]\n"
	if $Event::DebugLevel;
    exit $st;
}

my $got=0;
sub maybe_done { unloop if ++$got >= 2 }

my $child1;
unless($child1 = fork) { sleep $sleep; myexit 5 }
Event->process(callback => sub {
		   my $o = shift;
		   ok $o->{status}, 5 << 8; #unix only XXX?
		   ok $o->{pid}, $child1;

		   maybe_done();
	       });

my $child2;
unless ($child2 = fork) { sleep $sleep; myexit 6 }
Event->process(pid => $child2, callback => sub {
		   my $o = shift;
		   ok $o->{status}, 6 << 8;
		   ok $o->{pid} == $child2;

		   maybe_done();
	       });

warn "[entering loop]\n" if $Event::DebugLevel;
loop;
ok 1;

# try repeating? XXX
