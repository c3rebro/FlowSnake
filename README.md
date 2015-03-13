Programming test for the Thatgamecompany Systems Engineer position. The original prompt: 

Specification:
flOw MMo consists of 1000s of computer-controlled snakes vying for domination of the gene pool. We've jettisoned all the boring parts of flOw, like levels, mechanics, sound and actually playing the game, to focus on pure eat-or-be-eaten action. When the game begins, each snake is a single segment. The AI for each snake is to search for the nearest tail segment of another snake and swim towards it until it's in biting-range. When one snake bites another snake's tail, it fuses with that snake and becomes the new tail section. The feeding frenzy continues until only one giant snake remains, at which point its unsated bloodlust boils up inside it, causing it to explode back into individual segments, starting the whole gruesome parade over again.

The target platform for flOw MMo is the original Palm Pilot, so try to keep your game data under 128 KB. Market analysis, however, has shown that 99% of our target users have purchased the Pocket Rocket® coprocessor add-on, so feel free to use the full CPU power of your development PC or Mac for running the game. That said, flOw MMo is all about bite-a-minute action, so make sure the game stays locked at a silky 60 frames per second.

Tips:
flOw MMo doesn't need fancy graphics to pull you into the action. GL_POINTS and GL_LINES are your friends.
The primary focus should be code, data structures and optimization, not tuning numbers and colors. Hopefully, though, the end result will still be somewhat mesmerizing to watch.
The specification is not air tight. Fill in the gaps as you see fit. In the end, all that matters is that you are processing lots of snakes (hopefully 8k+ segments) cleanly and efficiently and it's kind of cool to watch.
Keep it simple. One .c or .cpp file. Allocate your memory statically. Use GLUT for windowing and minimal GL calls to draw, and try to avoid all other libraries as much as possible (memcpy is probably alright, but avoid complex things like malloc and sort).
