
package Event::io;

use Event;
use strict;
use IO::Poll qw(/POLL/);
use vars qw(@EXPORT_OK @ISA);
use Exporter ();

@EXPORT_OK = @IO::Poll::EXPORT_OK;
@ISA = qw(Exporter);

register Event;

my %cb = ();

sub new {
    my $class = shift;
    my %arg = @_;
    my $io = $arg{'-handle'};
    my $events = $arg{'-events'};
    my $callback = $arg{'-callback'};

    my $obj = bless {
	handle	  => $io,
	callback  => $callback,
	events	  => $events,
	cancelled => 0
    }, $class;

    $cb{$obj} = $obj;

    $obj;
}

sub cancel {
    my $self = shift;
    delete $cb{$self};
    $self->{'cancelled'} = 1;
}

sub prepare {
    my $self;
    foreach $self (values %cb) {
	Event::OS->AddSource($self->{'handle'}, $self->{'events'});
    }
    3600;
}

sub check {
    my $self;
    foreach $self (values %cb) {
	my $ev = Event::OS->SourceEvents($self->{'handle'}) & $self->{'events'};

	next unless $ev;

	my $priority = ($self->{'priority'} || ($ev & (POLLRDBAND | POLLWRBAND)))
		? 6 : 5;
	my @args = ($self,$self->{'handle'},$ev);
	my $sub = $self->{'callback'};
	Event->queuePriorityEvent($priority,
	    sub {
		$sub->(@args);
	    }
	);
    }
}

1;
