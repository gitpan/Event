# leak -*-perl-*-
use Test; plan test => 3;
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
#    my $var = 1;
#    Event->var(variable => \$var)->cancel;
}
for (1..2) { thrash(); }

my $got = join(', ', map { ref } all_watchers()) || 'None';
ok($got, 'None');
