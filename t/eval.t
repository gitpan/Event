# stop -*-perl-*- ?

use Test; plan tests => 12;
use Event qw(all_running loop unloop sweep);
# $Event::DebugLevel = 3;

my $status = 'ok';

my $die = Event->idle(e_cb => sub { die "died\n" }, e_desc => 'killer');

$Event::DIED = sub {
    my ($e,$why) = @_;

    ok $e->{e_desc}, 'killer';
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
eval { delete $die->{e_cb} };
ok $@, '/delete/';
ok exists $die->{e_cb};
ok !exists $die->{bogus};

# test 'e_' prefix detection
{
    my $warn='';
    $SIG{__WARN__} = sub { $warn .= $_[0] };
    $die->{e_reserved_key} = 1;
    ok $warn, '/reserved/';
    $warn='';
    ++$die->{e_reserved_key};
    ok $warn, '';
}

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
