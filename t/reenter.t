# deep -*- perl -*-
use Test; plan test => 2;
use Event qw(loop);

# $Event::DebugLevel = 3;

my $idle = Event->idle(repeat => 1,
		       callback => \&loop,
		       desc => "deep search");
do {
    local $SIG{__WARN__} = sub {};
    ok !defined loop();
};
ok $idle->{running}, 0;
