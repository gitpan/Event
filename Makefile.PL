# This -*- perl -*- script makes the Makefile
# $Id: Makefile.PL,v 1.3 1997/03/04 09:21:12 gbarr Exp $

require 5.002;
use ExtUtils::MakeMaker;

#--- MY package

sub MY::libscan
{
 my($self,$path) = @_;

 return '' 
	if($path =~ m:/(RCS|CVS|SCCS)/: ||
	   $path =~ m:[~%]$: ||
	   $path =~ m:\.(orig|rej)$:
	  );
 
 $path;
}

@Safe = (
	 OPTIMIZE => '-g',
	 CC => 'insure',
	 LD => 'insure',
	);

@Safe = ();

WriteMakefile(
	      PREREQ_PM => {
			    IO => 1.19,
			    Test => 1,
			   },
	      VERSION_FROM	=> "Event.pm",
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

