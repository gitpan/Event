# -*-perl-*-

use strict;
use Test; plan test => 2;
use Symbol;
use Fatal qw(open);
use Event qw(loop);

my $name = "./tail-test";
my ($r,$w) = (gensym,gensym);

open $w, ">$name";
select $w; $|=1; select STDOUT;
close $w;

open $r, $name;

Event->io(handle => $r, events => 'r',# tailpoll => 2,
	  callback => sub {
	      my ($e) = @_;
	      while (my $l = <$r>) {
		  warn $l;
	      }
	  });

my $c=0;
Event->timer(interval => .25, callback => sub {
		 open $w, ">>$name";
		 print $w $c++."\n";
		 close $w;
	     });

loop();

#END { unlink $name }
