# watch -*-perl-*-

use strict;
use Test; plan test => 5;
use Event qw(loop unloop);

# $Event::DebugLevel = 2;

my $var1 = 1;
my $var2 = 3;
my $var3 = 0;

Event->var(variable => \$var1, callback =>
	   sub {
	       my $var = shift->{'variable'};
	       ok $$var, 2;
	       $var2++;
	   },
	   desc => "var1"
);

Event->var(variable => \$var2, callback =>
	   sub {
	       $var3 = 3;
	       ok $var2, 4;
	       unloop;
	   },
	   desc => "var2");

Event->var(variable => \$var3, async => 1, callback => sub { ok $var3, 3; });

Event->idle(callback => sub {
		ok $var1, 1;
		$var1++;
	    });

my $var4 = 0;
my $e = Event->var(events => 'r', variable => \$var4, callback => sub {
		       my $e = shift;
		       ok $e->{count}, 1;
		   });
my $str = "$var4";  #read
$var4 = 5;          #write

loop;
