# process -*-perl-*-

use Test; plan tests => 5;
use Event qw(loop unloop);

#$Event::Eval = 1;
#$Event::DebugLevel = 3;

my $got=0;
sub maybe_done { unloop if ++$got >= 2 }

my $child1;
unless($child1 = fork) { sleep 1; exit 5 }
Event->process(callback => sub {
		   my $o = shift;
		   ok $o->{status}, 5 << 8; #unix only XXX?
		   ok $o->{pid}, $child1;

		   maybe_done();
	       });

my $child2;
unless ($child2 = fork) { sleep 1; exit 6 }
Event->process(pid => $child2, callback => sub {
		   my $o = shift;
		   ok $o->{status}, 6 << 8;
		   ok $o->{pid} == $child2;

		   maybe_done();
	       });

loop;
ok 1;

# try repeating? XXX
