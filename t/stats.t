# -*-perl-*-

use Test; plan test => 2;
use Event qw(loop unloop);

my $runs=0;
my $e;
$e = Event->idle(callback => sub {
		     ++$runs;
		     sleep 1;
		     unloop if ($e->stats(15))[0];
		 });
Event::Stats->restart();
loop;

my @s = $e->stats(15);
ok abs($s[0] - $runs) <= 1;
ok((abs($s[1] - $s[0]) < .1), 1, "$s[0] $s[1]");
