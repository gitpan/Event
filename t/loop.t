# -*-perl-*-

use strict;
use Test; plan tests => 2;
use Event qw(loop unloop);

#$Event::DebugLevel = 3;

my %got;
my $sleep = .1;
my $sleeping;
my $early = Event->idle(repeat => 1, callback => sub {
			    return if !$sleeping;
			    unloop 'early';
			});
Event->idle(repeat => 1, callback => sub {
		my $e = shift;
		$e->{reentrant} = 0;
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
		$e->{reentrant} = 1;
		$sleeping = 0;
		unloop if keys %got == 2;
	    });
loop;
