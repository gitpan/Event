use strict;
package Event::MakeMaker;
use Config;
use base 'Exporter';
use vars qw(@EXPORT_OK);
@EXPORT_OK = qw(&event_args);

my $installsitearch = $Config{sitearch};
$installsitearch =~ s,$Config{prefix},$ENV{PERL5PREFIX}, if
    exists $ENV{PERL5PREFIX};

sub event_args {
    my %arg = @_;
    $arg{INC} .= " -I$installsitearch/Event";
    %arg;
}

1;
