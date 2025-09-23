This is a programming project that is targeting iOS devices. The app combines the functionality of an Akai MPC sampler with Tetris. It uses JUCE and is written primarily in C++.

The idea is to have musical sessions where 8 audio files are loaded into buffers and then set to loop. These loops can be interacted with indirectly by selecting the corresponding pad for each buffer.

Every n bars (we'll default to four), a musical 'modifier' is applied to any of the buffers that have been selected by the user (the user pushes the buttons to make these selections). If the user does not select any buttons between the time the last modifier was applied and when the next modifier is applied, then the application will choose between 1-4 buffers to automatically apply the modifier the to.

Each buffer will run through its own effects chain before being summed in a master channel, which also has an effects chain.

Sometimes the modifier will affect the audio buffer (as in the case of buffer slicing, reversal of the playback direction, etc.) Other times, the modifier will affect the audio effect chain associated with the individual buffers. In other cases, the modifier will affect the master channel's effects chain (in which case, it won't matter what buttons the user pushes).

The upcoming modifer is displayed at the top of the screen and is randomly chosen.

Here is a list of modifiers to be implemented:

Buffer modifiers:

2.) Reverse - This reverses the current playback direction of the buffer.

3.) Speed - This multiplies the playback rate to either .25, .5, 1, or 2. (use timestretch if possible).

4.) Pitch up one octave.

5.) Pitch down one octave.

    Beat slicing - the buffer will be logically subdivided into 1/4 note, 1/8 note, 1/8 note triplet, 1/16 note, 1/32 note, and 1/64 note segments and the segments will be read in a random order. This must be done carefully and only use the original buffer.

Individual Buffer Audio Effect Modifiers:

1.) Turn on delay wet signal (over n bars). 1/4, 1/8, 1/8 triplet, 1/16, 1/32 note options.

2.) Turn on reverb wet signal (over n bars).

3.) Turn on low pass filter (over n bars).

4.) Turn on high pass filter (over n bars).

5.) Volume ramp down (over n bars).

6.) Tremolo (volume modulation) (over n bars).

Each of these effects should have a duration over which the current parameter value changes to the new parameter value. i.e. a reverb should turn on either immediately, over the course of 1 bar, 2 bars, or 4 bars. This length (1, 2, 4 etc.) should go up to the number of bars that the timing mechanism for the modifier queue uses (so if an option is set for the modifier to be applied ever 16 bars, then 8 and 16 bars should be an option here as well). The bar value is chosen at random.

Master Effects Modifiers:

1.) Turn on high-pass filter.

2.) Turn off low-pass filter.

This should operate similarly to the individual buffer audio effects modifiers.

Technically, these master effects should appear at the end of the audio chain for each channel so that the recording of individual tracks can happen correctly. Or, at least, this should happen in the case that multi-channel recording is turned on in the menu.

There should be a modifier that removes all other modifications, turns off all effects, and returns the playback speed/direction of the buffer to normal.

Expect more exotic modifiers to be added later. A flexible architecture should be developed that allows the programmer to combine modulation envelopes (slowly fade in or out over n bars, sine wave that ramps up in intensity over n bars, etc.) with various lengths of time (1 beat, 1 bar, 2 bars, etc.), stripe (effect turns on/off repeatedly every x beat division (32nd note, 16th note, 8th note, dotted 8th note, quarter note, etc).

There should be an options area where users can set:

1.) the number of bars that should pass before the next modifier

2.) BPM

3.) Add/change the audio files. This should allow the user to load files in using Apple's files app.

4.) Time signature

5.) Parts - each audio file can have up to 4 equal length parts (musical sections) labeled A-D. This is determined by the user, though it defaults to one part for simplicity.

5.) Theme

6.) About

7.) Multi-channel recording output (ie recording the master channel vs the output of the individual channels.

There should be project settings also:

1.) Load Project 2.) New Project 3.) Save Project 4.) Rename Project
