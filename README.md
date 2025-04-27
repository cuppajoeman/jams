# jams
just another midi sequencer

I like to make music, whenever I open the daw up you're limited to what can be done visually, midinous tries a new approach but doesn't scale up well, here's something new

## installing
So far I've only built this on linux, and you need to have `rtmidi` installed on your machine as I can't get it work with conan yet.

ubuntu
```
sudo apt install librtmidi-dev
```
## todo
* I want to make it so that we can record midi and then import it into a jam file, it would be like a command line thing where you record it specify if you want it in grid format, and then give it a pattern name. The point is that then you can record something live with an instrument and use that.
