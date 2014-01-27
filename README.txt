RemGlk: remote-procedure-call implementation of the Glk IF API

RemGlk Library: version 0.2.1.
Designed by Andrew Plotkin <erkyrath@eblong.com>
<http://eblong.com/zarf/glk/remglk.html>

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

The source code in this package is copyright 2012-4 by Andrew Plotkin.
You may copy and distribute it freely, by any means and under any
conditions, as long as the code and documentation is not changed. You
may also incorporate this code into your own program and distribute
that, or modify this code and use and distribute the modified version,
as long as you retain a notice in your program or documentation which
mentions my name and the URL shown above.

The RemGlk documentation is licensed under a Creative Commons
Attribution-Noncommercial-Share Alike 3.0 Unported License.
See <http://creativecommons.org/licenses/by-nc-sa/3.0>

