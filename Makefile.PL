# This -*- perl -*- script makes the Makefile

# hope this is right!  :-)
if ($] < 5.00503 or ($] > 5.0055 and $] < 5.00552)) {
    warn "
  ** WARNING ** REQUIRES NEW SELF-TIE PATCH ** WARNING **

  (see patches/leak.fix for a copy)

";
}

use ExtUtils::MakeMaker;

# Parasoft's Insure++!
push @Safe, OPTIMIZE => '-g' if 0;
push @Safe, CC => 'insure', LD => 'insure' if 0;

@PREFIX = (PREFIX => $ENV{PERL5PREFIX})
    if exists $ENV{PERL5PREFIX};

my @opt=(@PREFIX,
	 PREREQ_PM => { Test => 1 },
	 VERSION_FROM	=> "./lib/Event.pm",
	 NAME      	=> "Event",
	 INC		=> '-Ic -Ilib/Event',
	 H		=> [glob("c/*"), glob("*.h"),
			    "lib/Event/EventAPI.h"],
	 @Safe,
	 
	 'linkext' 	=> {LINKTYPE => 'dynamic' }, #maybe static ok?
	 
	 'clean'   	=> {FILES => join(" ",
					  map { "$_ */$_ */*/$_" }
					  qw(*% *.html *.b[ac]k *.old *.orig))
			   },
	);

WriteMakefile(@opt);
