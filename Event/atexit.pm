
package Event::atexit;

sub new {
    my $self = shift;
    my %arg = @_;
    my $cb = $arg{'-callback'};

    
    my $obj = bless \$cb, $self;

    $obj;
}

sub cancel {
    my $self = shift;
    undef $$self;
}

1;
