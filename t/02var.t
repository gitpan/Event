
use Event;

print "1..5\n";

my $var1 = 1;
my $var2 = 3;
my $var3 = 0;

Event->watchvar(
    -variable => \$var1,
    -callback =>
	sub {
	    print "ok ",$var1,"\n";
	    $var2++
	}
);

Event->watchvar(
    -variable => \$var2,
    -callback =>
	sub {
	    $var3 = 3;
	    print "ok ",$var2,"\n";
	    Event->exit;
	}
);

Event->watchvar(
    -variable => \$var3,
    -async    => 1,
    -callback =>
	sub {
	    print "ok ",$var3,"\n";
	}
);

Event->idle(
    sub {
	print "ok ",$var1,"\n";
	$var1++;
    }
);

Event->Loop;

print "ok 5\n";
