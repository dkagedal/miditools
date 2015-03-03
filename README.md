# miditools
A couple of simple MIDI utilities.

## autoconnect
This utility watches connecting MIDI devices and automatically connect device ports together based on their name.
The usage is

    autoconnect [-v] src dest src dest ...

An example:

    autoconnect -v \
	    'Channel Enforcer':4 aeolus \
	    'Keystation 61e' 'Channel Enforcer':0 \
	    'eKeys-49 USB MIDI Keyboard' 'Channel Enforcer':1 \
	    'UM-ONE' 'Channel Enforcer':3 &

In this example 'Channel Enforcer' is the name used by the midichan utility.

## midichan
This utility rewrites MIDI messages to change the MIDI channel. It creates a set of input ports, and for each of
theses ports, assigns a MIDI channel to rewrite messages to, disregardgin the existing channel information completely.

The simplest way to use it is to simply to

    midichan [-v] -n 4

which creates four input ports mapped to channel 1, 2, 3 and 4 (or 0, 1, 2, 3 depending on how you count).
It is also possible to map explicitly

    midichan -v -m 1,3,5,4
