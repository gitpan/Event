# stop -*-perl-*- ?

use Test; plan tests => 8;
use Event qw(all_running loop unloop);
#$Event::DebugLevel = 3;

my $status = 'ok';

my $die = Event->idle(callback => sub { die "died\n" }, desc => 'killer');

$Event::DIED = sub {
    my $e = all_running();
    my $why = $@;

    ok $e->{desc}, 'killer';
    chop $why;
    ok $why, 'died';

    if ($Event::Eval == 0) {
	$Event::Eval = 1;
	$die->again
    } elsif ($Event::Eval == 1) {
	unloop $status;
    }
};

# simple stuff
delete $die->{bogus};
eval { delete $die->{callback} };
ok $@, '/delete/';
ok exists $die->{callback};
ok !exists $die->{bogus};

ok loop(), $status;
