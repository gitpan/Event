# stop -*-perl-*- ?

use Test; plan tests => 8;
use Event;
#$Event::DebugLevel = 3;

my $status = 'ok';

my $die = Event->idle(callback => sub { die "died\n" }, desc => 'killer');

$Event::DIED = sub {
    my ($e,$why) = @_;

    ok $e->{desc}, 'killer';
    chop $why;
    ok $why, 'died';

    if ($Event::Eval == 0) {
	$Event::Eval = 1;
	$die->again
    } elsif ($Event::Eval == 1) {
	Event::Loop::exitLoop($status);
    }
};

# simple stuff
delete $die->{bogus};
eval { delete $die->{callback} };
ok $@, '/delete/';
ok exists $die->{callback};
ok !exists $die->{bogus};

ok Event::Loop::Loop(), $status;
