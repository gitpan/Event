# -*-perl-*-

use strict;
use Test; plan tests => 3;
use Event qw(loop unloop);

# $Event::DebugLevel = 2;

my %got;
my $sleep = 1;
my $sleeping;
my $early = Event->idle(repeat => 1, cb => sub {
			    return if !$sleeping;
			    unloop 'early';
			});
Event->idle(desc => "main", repeat => 1, cb => sub {
		my $e = shift;
		$e->w->reentrant(0);
		$sleeping = 1;
		my $ret = loop($sleep);
		if (!exists $got{$ret}) {
		    $got{$ret} = 1;
		    if ($ret eq 'early') {
			$early->cancel;
			ok 1;
		    } elsif ($ret == $sleep) {
			ok 1;
		    }
		}
		$e->w->reentrant(1);
		$sleeping = 0;
		unloop(0) if keys %got == 2;
	    });

ok loop, 0;
