# idle daydreams of -*-perl-*-

use Test; plan tests => 5;
use Event qw(loop unloop);

# $Event::Eval = 1;
#$Event::DebugLevel = 0;

package myobj;
use Test;

my $myobj;
sub idle {
    my ($o,$e) = @_;
    if (!$myobj) {
	# see if method callbacks work
	ok $o, 'myobj';
	ok $e->isa('Event::Watcher');
	ok $e->{e_desc}, __PACKAGE__;
    }
    ++$myobj;
}
Event->idle(e_cb => [__PACKAGE__,'idle'],
	    e_desc => __PACKAGE__);

package main;

my $count=0;
my $idle = Event->idle(e_desc => "exit", e_cb =>
		       sub {
			   my $e = shift;
			   ++$count;
			   unloop() if $count > 2 && $myobj;
		       });

ok ref($idle), 'Event::idle';

Event->idle(e_cb => sub { ok 0; Event->exit })->cancel;
Event->idle(e_cb => sub { $idle->again }, e_repeat => 1, e_desc => "again");

ok !defined loop();
