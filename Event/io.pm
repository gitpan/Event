use strict;
package Event::io;
BEGIN { 'Event::Loop'->import(qw(PRIO_NORMAL queueEvent)); }

my (%cb,@cb);

'Event'->register(check => sub {
    # OPTIMIZE
    # need a faster mapping from file descriptor to event objects XXX

    for my $o (@cb) {
	next unless Event::OS::GotEvent($o->{'handle'}, $o->{'events'});
	queueEvent($o);
    }
});

sub new {
#    lock %Event::;

    shift;
    my %arg = @_;
    for (qw(desc handle events callback)) {
	$arg{$_} = $arg{"-$_"} if exists $arg{"-$_"};
    }

    $arg{priority} = PRIO_NORMAL + ($arg{priority} or 0);
    my $obj = bless \%arg, __PACKAGE__;
    $cb{$obj} = $obj;
    @cb = values %cb;
    Event::OS::AddSource($obj->{'handle'}, $obj->{'events'});
    Event::init($obj);
}

sub cancel {
#    lock %Event::;

    my $self = shift;
    delete $cb{$self};
    @cb = values %cb;
    Event::OS::RemoveSource($self->{'handle'}, $self->{'events'});
    $self->SUPER::cancel();
}

1;
