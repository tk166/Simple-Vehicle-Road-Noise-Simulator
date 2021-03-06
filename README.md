# Simple Vehicle Road Noise Simulator
A simple but realistic simulator of vehicle air noise and road noise on flat road with the consideration  of varying vehicle speed, other vehicles passing by and tire skids.

Designed for the use of driving simulators. The sound simulator was written in C++ and compiled with MinGW 11.0. 

The simulator is base on SDL(http://www.libsdl.org/). 
A controller is needed to generate simulator's parameters, an example was given in Simulink. Any other languages or softwares that can sent UDP messages are able to set simulator's parameters.

![User Interface](https://github.com/tk166/Vehicle-Road-Noise-Simulator/blob/main/pics/P0001.png)


Note1:

PCM sound data in the "pcm_sound" folder is not provided for copyright reasons.

Please refer to https://mynoise.net/NoiseMachines/trafficNoiseGenerator.php and purchase related resources.

10 PCM sound files are needed here to generate the correct sound. All of them are in the S16LE(S16LSB) PCM format with a sample rate of 44100Hz and only one channel.

The first 9 files needed named from "NF01.pcm"  to "NF09.pcm"， every "NFxx.pcm" file contains sound that generated by setting the xx th bar on this website with the highest volume wihle the rest 9 bars with the lowest  volume. 

The last file named "TS.pcm" just recorded the stable tire skid sound of a vehicle, and was provided for free here.
