#!./perl -w

use Qt 2.0;
use Event;
use NetServer::ProcessTop;

package MyMainWindow;
use base 'Qt::MainWindow';

use Qt::slots 'quit()';
sub quit {
    Event::unloop(0);
}

package main;
import Qt::app;

Event->io(desc => 'Qt', fd => Qt::xfd(), timeout => .25,
	  cb => sub {
	      $app->processEvents(3000);  #read
	      $app->flushX();             #write
	  });

my $w = MyMainWindow->new;

my $file = Qt::PopupMenu->new;
$file->insertItem("Quit", $w, 'quit()');
my $mb = $w->menuBar;
$mb->insertItem("File", $file);

my $at = 1000;
my $label = Qt::Label->new("$at", $w);
$w->setCentralWidget($label);

Event->timer(interval => .25, cb => sub {
		 --$at;
		 $label->setText($at);
	     });

$w->resize(200, 200);
$w->show;

$app->setMainWidget($w);
exit Event::loop();

