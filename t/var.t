# watch -*-perl-*-

use strict;
use Test; plan test => 5;
use Event qw(loop unloop);

# $Event::DebugLevel = 2;

my $var1 = 1;
my $var2 = 3;
my $var3 = 0;

Event->var(e_var => \$var1, e_cb =>
	   sub {
	       my $var = shift->{'e_var'};
	       ok $$var, 2;
	       $var2++;
	   },
	   e_desc => "var1"
);

Event->var(e_var => \$var2, e_cb =>
	   sub {
	       $var3 = 3;
	       ok $var2, 4;
	       unloop;
	   },
	   e_desc => "var2");

Event->var(e_var => \$var3, e_async => 1, e_cb => sub { ok $var3, 3; });

Event->idle(e_cb => sub {
		ok $var1, 1;
		$var1++;
	    });

my $var4 = 0;
my $e = Event->var(e_poll => 'r', e_var => \$var4, e_cb => sub {
		       my $e = shift;
		       ok $e->{e_hits}, 1;
		   });
my $str = "$var4";  #read
$var4 = 5;          #write

loop;
