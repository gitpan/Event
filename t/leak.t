# leak -*-perl-*-
use Test; plan test => 4;
use Event qw(all_watchers);

my @e = Event::all_watchers();
ok @e, 0;
@e = Event::all_idle();
ok @e, 0;

sub thrash {
    Event->idle()->cancel;
    Event->io()->cancel;
    Event->signal(signal => 'INT')->cancel;
    Event->timer(at => time)->cancel;
    my $var = 1;
    Event->var(var => \$var)->cancel;
}
for (1..2) { thrash(); }

my $got = join(', ', map { ref } all_watchers()) || 'None';
ok($got, 'None');

{
    my $io = Event->io();
    $io->cancel for 1..3;  #shouldn't crash!
    ok 1;
}
