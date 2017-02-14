RemGlk: remote-procedure-call implementation of the Glk IF API

RemGlk Library: version 0.2.6.
Designed by Andrew Plotkin <erkyrath@eblong.com>
<http://eblong.com/zarf/glk/remglk/docs.html>

This is source code for an implementation of the Glk library which
supports structured input and output.

RemGlk does not provide a user interface. Instead, it wraps up the
application's output as a JSON data structure and sends it to stdout.
It then waits for input to arrive from stdin; the input data must also
be encoded as JSON.

RemGlk is therefore like CheapGlk, in that it works entirely through
input and output streams, and can easily be attached to a bot or web
service. However, unlike CheapGlk, RemGlk supports multiple Glk
windows and most Glk I/O features. Whatever it's attached to just has
to decode the structured output and display it appropriately.


* Permissions

The RemGlk library is copyright 2012-17 by Andrew Plotkin. The
GiDispa and GiBlorb libraries, as well as the glk.h header file, are
copyright 1998-2017 by Andrew Plotkin. The GiDebug library is copyright
2014-2017 by Andrew Plotkin. All are distributed under the MIT license;
see the "LICENSE" file.

The RemGlk documentation is licensed under a Creative Commons
Attribution-Noncommercial-Share Alike 3.0 Unported License.
See <http://creativecommons.org/licenses/by-nc-sa/3.0>

