
package Event::signal;

use Event;
use strict;

registerAsync Event;

my %sig;

BEGIN {

    # populate %sig

    my @sig = grep { /^[A-Z]/ } map { RealSigName($_) } keys %SIG;
    @sig{@sig} = (undef) x @sig;

    # delete un-trapable signals

    delete $sig{KILL};
    delete $sig{STOP};
    delete $sig{ZERO};

    # and delete SIGCHLD as it is handled by Event::Process

    my $chld = RealSigName('CHLD') || RealSigName('CLD');
    delete $sig{$chld} if defined $chld;
}

sub new {
    my $self = shift;
    my %arg = @_;
    my $name = $arg{'-signal'};
    my $cb = $arg{'-callback'};

    # only accept callbacks for signals in %sig
    return undef
	unless exists $sig{$name};

    my $obj = bless {
	signal   => $name,
	callback => $cb,
	canceled => 0
    }, $self;

    push( @{$sig{$name} ||= []}, $obj);

    _watch_signal($name);

    $obj;
}

sub cancel {
    my $self = shift;
    my $name = $self->{'signal'};
    $self->{'canceled'} = 1;
    $sig{$name} = [ grep { $_ == $self ? undef : $_ } @{$sig{$name}} ];
    _unwatch_signal($name)
	unless @{$sig{$name}};
}

sub check {
    my @val = _reap();

    while(@val) {
	my($name,$count) = splice(@val,0,2);
	if (defined $sig{$name}) {
	    my $cb;
	    while($count--) {
		foreach $cb (@{$sig{$name}}) {
		    next if $cb->{'canceled'};

		    my $sub = $cb->{'callback'};
		    Event->queueAsyncEvent(
			sub {
			    $sub->($cb,$name)
			}
		    );
		}
	    }
	}
    }
}

1;
