# stop -*-perl-*- ?

use Test; plan tests => 10;
use Event qw(all_running loop unloop sweep);
# $Event::DebugLevel = 3;

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
	unloop $status;
	$Event::Eval = 0;
    }
};

# simple stuff
delete $die->{bogus};
eval { delete $die->{callback} };
ok $@, '/delete/';
ok exists $die->{callback};
ok !exists $die->{bogus};

ok loop(), $status;

#-----------------------------

{
    local $Event::DIED = sub { die };
    local $SIG{__WARN__} = sub {
	ok $_[0], '/Event::DIED died/';
    };
    $die->now;
    sweep();
}
{
    local $Event::DIED = \&Event::verbose_exception_handler;
    local $SIG{__WARN__} = sub {
	ok $_[0], '/died/';
    };
    $die->now;
    sweep();
}
