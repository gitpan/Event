#!./perl -w

use strict;
use Test; plan tests => 8;
use Event 0.28 qw(sweep);

# $Event::DebugLevel = 2;

sub inspect {
    my $o = shift;
    my %k;
    for my $k (keys %$o) {
	die "Got $k twice in $o" if exists $k{$k};
	$k{$k}=1;
    }
    ok 1;
    ++$o->{retries};
    $o;
}

inspect(Event->timer(e_after => 0, e_cb => \&inspect));
inspect(Event->io(e_timeout => .001, e_fd => \*STDIN, e_poll => 'r',
			   e_cb => \&inspect));
my $ev;
my $timer = Event->timer(e_after => 0, e_cb => sub {
			     $ev = shift;
			     ok exists $ev->{e_hits};
			 },
			 stuff => 'stuff');
ok $timer->{stuff}, 'stuff';

sweep();

# $ev has morphed from an event into a watcher
ok !exists $ev->{e_hits};
ok $timer->{e_id}, $ev->{e_id};