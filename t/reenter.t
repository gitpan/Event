# deep -*- perl -*-
use Test; plan test => 7;
use Event qw(loop unloop one_event all_running);

# $Event::DebugLevel = 2;

# deep recursion
my $idle = Event->idle(repeat => 1, min_interval => undef,
		       callback => \&loop);
do {
    local $SIG{__WARN__} = sub {};
    ok !defined loop();
};
ok $idle->{running}, 0;


# simple nested case
my $nest=0;
$idle->{callback} = sub {
    return if ++$nest > 10;
    one_event();
};
ok one_event();


# a bit more complex nested exception
$nest=0;
$idle->{callback} = sub {
    die 10 if ++$nest > 10;
    one_event() or die "not recursing";
};
$Event::DIED = sub {
    my $e = shift;
    ok $e->{id}, $idle->{id};
    ok $e->{id}, all_running()->{id},
    my @all = all_running;
    ok @all, $nest;
    unloop();
};
loop();
ok 1;
