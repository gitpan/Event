use strict;
package Event::io;
use vars qw(@ISA @EXPORT_OK);
@ISA = qw(Event::Watcher Exporter);
@EXPORT_OK = qw(R W E T);  # bit constants

'Event::Watcher'->register;

sub new {
#    lock %Event::;

    shift if @_ & 1;
    my %arg = @_;

    my $o = allocate();
    $o->init(\%arg);
    $o->start;
    $o;
}

{
    # deprecated
    no strict 'refs';
    my $warn = 5;
    for my $f (qw(R W E T)) {
	*{$f} = sub {
	    Carp::carp "Please use Event::$f instead" if --$warn >= 0;
	    &{"Event::$f"};
	};
    }
}

1;
