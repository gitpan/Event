# -*-perl-*-

use Test; BEGIN { plan test => 2, todo => [2] }
use Event;
ok 1;
eval {
    # This doesn't work because there is no way to tell
    # the exporter about autoloaded functions.  True?

    # don't know how to turn off exporter warnings!
    Event->import(qw(io process timer signal var)); 
};
ok $@, '';
