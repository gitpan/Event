# -*-perl-*-

use Test; BEGIN { plan test => 2, todo => [2] }
use Event;
ok 1;
eval {
    # don't know how to turn off exporter warnings!
    Event->import(qw(io process timer signal var)); 
};
ok $@, '';
