package Event::OS::default;

use Carp;
use IO::Poll;

my $poll;

sub PrepareSource {
    $poll = new IO::Poll;
}

sub ClearSource {
    undef $poll;
}

sub WaitForEvent {
    shift;
    my $timeout = shift;

    $timeout = 0
	if($timeout && ($timeout < 0));

    if($poll->handles) {
	$poll->poll($timeout);
    }
    else {
	sleep($timeout);
    }
}

sub AddSource {
    shift;
    my($obj,$event) = @_;

    if(ref($obj) && UNIVERSAL::isa($obj,'IO::Handle')) {
	my $mask = $poll->mask($obj) || 0;

	$poll->mask($obj, $mask | $event );
	return 1;
    }

    croak "Unknown source object $obj";
}

sub SourceEvents {
    shift;
    my $obj = shift;
    
    if(ref($obj) && UNIVERSAL::isa($obj,'IO::Handle')) {
	return $poll->events($obj) || 0;
    }

    croak "Unknown source object $obj";
}

1;
