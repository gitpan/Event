# leak -*-perl-*-
use Test; plan test => 1;
use Event qw(all_watchers);

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
