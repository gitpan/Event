
package Event::watchvar;

use strict;
require Event;

register Event;

sub prepare { 3600 }
sub check {}

sub new {
    my $self = shift;
    my %opt = @_;
    my $ref = $opt{'-variable'};
    my $cb  = $opt{'-callback'};

    my $obj = bless {
	variable => $ref,
	callback => $cb
    }, $self;

    unless($opt{-async}) {
	$obj->{'-callback'} = sub {
	    Event->queueEvent(sub { $cb->($obj) })
	}
    }

    $obj->_watchvar;
    $obj;
}

sub cancel {
    shift->_unwatchvar;
}

1;
