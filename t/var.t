# watch -*-perl-*-

use Event qw(loop unloop);

# $Event::DebugLevel = 2;

print "1..5\n";

my $var1 = 1;
my $var2 = 3;
my $var3 = 0;

Event->var(
    -variable => \$var1,
    -callback =>
	sub {
	    print "ok ",$ {shift->{'-variable'}},"\n";
	    $var2++
	    },
    desc => "var1"
);

Event->var(
    -variable => \$var2,
    -callback =>
	sub {
	    $var3 = 3;
	    print "ok ",$var2,"\n";
	    unloop;
	},
		desc => "var2"
);

Event->var(
    -variable => \$var3,
    nice    => -10,
    -callback =>
	sub {
	    print "ok ",$var3,"\n";
	},
		desc => "var3"
);

Event->idle(
    -callback => sub {
	print "ok ",$var1,"\n";
	$var1++;
    },
	    desc => "idle"
);

loop;

print "ok 5\n";
