use strict;
package Event::process;
use base 'Event::Watcher';
use vars qw($DefaultPriority);
$DefaultPriority = Event::PRIO_HIGH();

BEGIN { 'Event::Watcher'->import(qw(ACTIVE)); }
'Event::Watcher'->register();

sub new {
    #lock %Event::;

    shift if @_ & 1;
    my %arg = @_;
    my $o = 'Event::process'->allocate();
    $o->init([qw(pid)], \%arg);
    $o->{any} = 1 if !exists $o->{pid};
    $o->start();
    $o;
}

my %cb;		# pid => [events]

Event->signal(signal => 'CHLD',  #CLD? XXX
	      callback => sub {
		  my ($o) = @_;
		  for (my $x=0; $x < $o->{count}; $x++) {
		      my $pid = wait;
		      last if $pid == -1;
		      my $status = $?;
		      
		      my $cbq = delete $cb{$pid} if exists $cb{$pid};
		      $cbq ||= $cb{any} if exists $cb{any};
		      
		      next if !$cbq;
		      for my $e (@$cbq) {
			  $e->{pid} = $pid;
			  $e->{status} = $status;
			  Event::queueEvent($e)
		      }
		  }
	      },
	      desc => "Event::process SIGCHLD handler");

sub start {
    my ($o, $repeat) = @_;
    $o->{flags} |= ACTIVE;
    my $key = exists $o->{any}? 'any' : $o->{pid};
    push @{$cb{ $key } ||= []}, $o;
}

sub stop {
    my $o = shift;
    $o->{flags} &= ~ACTIVE;
    my $key = exists $o->{any}? 'any' : $o->{pid};
    $cb{ $key } = [grep { $_ != $o } @{$cb{ $key }} ];
    delete $cb{ $key } if
	@{ $cb{ $key }} == 0;
}

1;
