use strict;
package Event::watchvar;
BEGIN { 'Event::Loop'->import(qw(PRIO_NORMAL queueEvent)); }

'Event'->register;

sub prepare { 3600 }
sub check {}

sub new {
    # lock %Event::;

    my $self = shift;
    my %arg = @_;

    for (qw(variable callback async)) {
	$arg{$_} = $arg{"-$_"} if exists $arg{"-$_"};
    }

    my $ref = $arg{'variable'};
    my $cb  = $arg{'callback'};

    $arg{priority} = PRIO_NORMAL + ($arg{priority} or 0);

    my $obj = bless \%arg, __PACKAGE__;

    my $sub;
    if (!$Event::DebugLevel) {
	$sub = sub { $cb->($obj,$ref) };
    } else {
	$sub = sub {
	    Event::invoking($obj);
	    $cb->($obj,$ref);
	    Event::completed($obj);
	};
    }

    $obj->{'callback'} = ($arg{async}?
			  $sub : sub { queueEvent($obj->{priority}, $sub) });

    $obj->_watchvar;

    Event::init($obj);
}

sub cancel {
    my $self = shift;
    $self->_unwatchvar;
    $self->SUPER::cancel();
}

1;
