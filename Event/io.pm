use strict;
package Event::io;
BEGIN { 'Event::Loop'->import(qw(PRIO_NORMAL queueEvent)); }

'Event'->register;

my (%cb,@cb);

sub new {
#    lock %Event::;

    shift;
    my %arg = @_;
    for (qw(handle events callback)) {
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
    $self->{'cancelled'} = 1;
}

sub prepare { 3600 }

sub check {
    # have a faster mapping from file descriptor to event object? XXX

    for my $o (@cb) {
	next unless Event::OS::GotEvent($o->{'handle'}, $o->{'events'});

	my $cb = $o->{'callback'};
	my $sub;
	if (!$Event::DebugLevel) {
	    $sub = sub {
		$cb->($o);
	    };
	} else {
	    $sub = sub {
		Event::invoking($o);
		$cb->($o);
		Event::completed($o);
	    };
	}
	queueEvent($o->{'priority'}, $sub);
    }
}

1;
