# stop -*-perl-*- ?

use Test; plan tests => 5;
use Event;
$Event::Eval = 1;

my $normal=0;
my $die = Event->idle(callback => sub { die });
Event->idle(callback => sub { ++$normal; Event->exit });

delete $die->{bogus};
eval { delete $die->{callback} };
ok $@, '/delete/';

ok exists $die->{callback};
ok !exists $die->{bogus};

ok eval {
    Event->Loop;
    1;
};

ok $normal;
