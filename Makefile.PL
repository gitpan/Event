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
	      INC => '-Ic -Ilib/Event',
	      H => [glob("c/*"), glob("*.h"), "lib/Event/EventAPI.h"],
	      @Safe,
 
	'linkext' 	=> {LINKTYPE => 'dynamic' }, #maybe static ok?
 
	'clean'   	=> {FILES => join(" ",
					map { "$_ */$_ */*/$_" }
					qw(*% *.html *.b[ac]k *.old *.orig))
			   },
);

