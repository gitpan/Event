use strict;
package Event::watchvar;
BEGIN { 'Event::Loop'->import(qw(PRIO_NORMAL queueEvent)); }

'Event'->register;

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
    $arg{priority} -= 10 if $arg{async};

    my $obj = bless \%arg, __PACKAGE__;

    $obj->{'modified'} = sub { queueEvent($obj, $ref) };
    $obj->_watchvar;

    Event::init($obj);
}

sub cancel {
    my $self = shift;
    $self->_unwatchvar;
    $self->SUPER::cancel();
}

1;
