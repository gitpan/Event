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
__END__

=head1 NAME

Event::MakeMaker - MakeMaker glue for the C-level Event API

=head1 SYNOPSIS

This is an advanced feature.

=head1 DESCRIPTION

If you need optimal performance, you can hook into Event at the
C-level.  You'll need to make changes to your C<Makefile.PL> and add
code to your C<xs> or C<c> file(s).

=head1 WARNING

When you hook in at the C-level, you get a I<huge> performance gain
but you reduce the chances that your code will work unmodified with
newer versions of perl or L<Event>.  This may or not be a problem.
Just be aware of it and set your expectations accordingly.

=head1 HOW TO

=head2 Makefile.PL

  use Event::MakeMaker qw(event_args);

  # ... set up %args ...

  WriteMakefile(event_args(%args));

=head2 XS

  #include "EventAPI.h"
  static struct EventAPI *Ev=0;

  BOOT:
    FETCH_EVENT_API("YourModule", Ev);

=head2 API (v9)

 struct EventAPI {

  /* EVENTS */
  void (*start)(pe_event *ev, int repeat);
  void (*queue)(pe_event *ev, int count);
  void (*now)(pe_event *ev);
  void (*suspend)(pe_event *ev);
  void (*resume)(pe_event *ev);
  void (*cancel)(pe_event *ev);

  pe_idle     *(*new_idle)();
  pe_timer    *(*new_timer)();
  pe_io       *(*new_io)();
  pe_var      *(*new_var)();
  pe_signal   *(*new_signal)();

  /* TIMEABLE */
  void (*tstart)(pe_timeable *);
  void (*tstop)(pe_timeable *);

  /* HOOKS */
  pe_qcallback *(*add_hook)(char *which, void *cb, void *ext_data);
  void (*cancel_hook)(pe_qcallback *qcb);

 };

=head2 EXAMPLE

  static pe_io *X11_ev=0;

  static void x_server_dispatch(void *ext_data)
  { ... }

  if (!X11_ev) {
    X11_ev = Ev->new_io();
    X11_ev->events = PE_R;
    sv_setpv(X11_ev->base.desc, "X::Server");
    X11_ev->base.callback = (void*) x_server_dispatch;
    X11_ev->base.ext_data = <whatever>;
    X11_ev->base.priority = PE_PRIO_NORMAL;
  }
  X11_ev->fd = x_fd;
  Ev->resume((pe_event*) X11_ev);
  Ev->start((pe_event*) X11_ev, 0);

=head2 BUT I NEED A NEW TYPE OF WATCHER FOR MY INTERGALACTIC INFEROMETER

Are you sure?  Hopefully you can just do something like this:

  struct xevent {
    pe_io *io;
    XEvent event;
    ...etc...
  };

To create it you'll need to:

  struct xevent *xe;
  New(xe, 0, 1, struct xevent);
  xe->io = Ev->new_io();
  xe->io->ext_data = xe;   /* note: circular ref */
  ...

I'd prefer not to export the entire Event.h apparatus in favour of
minimizing interdependencies.  If you really, really need to create a
new type of watcher send your problem analysis to the mailing list.

=cut
