# -*-perl-*-

use strict;
use Test; plan test => 3;
use Symbol;
use Fatal qw(open);
use Event qw(loop unloop);

my $name = "./tail-test";
my ($r,$w) = (gensym,gensym);

open $w, ">$name";
select $w; $|=1; select STDOUT;
close $w;

open $r, $name;

my $hit=0;
my $io = Event->io(handle => $r, events => 'r', tailpoll => .05,
	  callback => sub {
	      my ($e) = @_;
	      ++$hit;
	      while (my $l = <$r>) {
		  # ignore
	      }
	  });
ok $io->{size}, 0;

my $c=0;
Event->timer(interval => .2, callback => sub {
		 # without tailpoll this gets called repeatedly -- busy wait!
		 unloop() if $c == 4;
		 open $w, ">>$name";
		 binmode $w;
		 print $w $c++."\n";
		 close $w;
	     });

loop();

ok $hit, 4;
ok $io->{size}, 8;

END { unlink $name }
