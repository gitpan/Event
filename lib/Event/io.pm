use strict;
package Event::io;
use vars qw(@ISA @EXPORT_OK @ATTRIBUTE);
@ISA = qw(Event::Watcher Exporter);
@EXPORT_OK = qw(R W E T);  # bit constants
@ATTRIBUTE = qw(poll fd timeout);

'Event::Watcher'->register;

sub new {
#    lock %Event::;

    my $o = allocate(shift);
    my %arg = @_;
    $o->init(\%arg);
    $o->start;
    $o;
}

1;
