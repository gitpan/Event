use strict;
package Event::io;
use vars qw(@ISA @EXPORT_OK);
@ISA = qw(Event::Watcher Exporter);
@EXPORT_OK = qw(R W E);  # bit constants

'Event::Watcher'->register;

sub new {
#    lock %Event::;

    shift if @_ & 1;
    my %arg = @_;

    my $o = allocate();
    $o->init([qw(handle events)], \%arg);
    $o->start;
    $o;
}

1;
