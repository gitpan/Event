# This -*- perl -*- script makes the Makefile

require 5.00405;
use ExtUtils::MakeMaker;

# Parasoft's Insure++!
@Safe = (
	 OPTIMIZE => '-g',
	 CC => 'insure',
	 LD => 'insure',
	)
    if 0;

@PREFIX = (PREFIX => $ENV{PERL5PREFIX})
    if exists $ENV{PERL5PREFIX};

WriteMakefile(
	      @PREFIX,
	      PREREQ_PM => {
			    IO => '1.19',
			    Test => 1,
			   },
	      VERSION_FROM	=> "./lib/Event.pm",
	      NAME      	=> "Event",
	      INC => '-Ic',
	      H => [glob("c/*"), glob("*.h")],
	      @Safe,
 
	'linkext' 	=> {LINKTYPE => 'dynamic' },
 
	'clean'   	=> {FILES => join(" ",
					map { "$_ */$_ */*/$_" }
					qw(*% *.html *.b[ac]k *.old *.orig))
			   },
);

