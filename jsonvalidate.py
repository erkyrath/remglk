#!/usr/bin/env python3

# Read a stream of JSON from stdin. Display each one as it is read.
# If a syntax error occurs, it won't actually be detected; the output
# will just freeze up.

import sys
import json

dec = json.JSONDecoder()

dat = ''

while True:
    ln = sys.stdin.readline()
    if (not ln):
        break
    dat = dat + ln
    try:
        (obj, pos) = dec.raw_decode(dat)
        dat = dat[ pos : ]
        print(obj)
    except:
        pass
