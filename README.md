## Craft

Minecraft clone for Linux and Mac OS X. Just a few thousand lines of C using
modern OpenGL (shaders). The server does all world processing; you need a
server to play singleplayer.

Client based on http://www.michaelfogleman.com/craft/.

### Features

* World generation and processing done entirely on the server.
* More than 10 types of blocks and more can be added easily.
* Supports full and partial transparency (glass/water).
* Multiplayer support!

### Download

See below to run from source.

### Install Dependencies

#### Mac OS X

Notice that this should work, but it is untested.

Download and install [GLFW](http://www.glfw.org) and
[GLEW](http://glew.sourceforge.net) if you don't already have them. You may use
[Homebrew](http://brew.sh) to simplify the installation:

    brew install glfw3 
    brew install glew 

#### Linux
##### Arch

    sudo pacman -S glfw-x11 glew

##### Ubuntu

    sudo apt-get install libglew-dev
    sudo apt-get build-dep glfw

#### Windows

Windows builds are broken for now. You may try to use Cygwin or MinGW's MSYS to
build as a pseudo-Unix target, but that is untested.

### Compile and Run

Once you have the dependencies (see above), run the following commands in your
terminal.

    git clone https://github.com/Min4Builder/Craft
    cd Craft
    make

Notice that you need the server, even when playing singleplayer. Once that's
running, run:

    make run

#### Online servers

You can connect to an online server with command line arguments...

    ./craft craft.michaelfogleman.com

Or, with the "/server" command in the game itself.

    /server craft.michaelfogleman.com

### Controls

- WASD to move forward, left, backward, right.
- Space to jump.
- Left Click to destroy a block.
- Right Click or Cmd + Left Click to create a block.
- Ctrl + Right Click to toggle a block as a light source.
- 1-9 to select the block type to create.
- E to cycle through the block types.
- Tab to toggle between walking and flying.
- ZXCVBN to move in exact directions along the XYZ axes.
- Left shift to zoom.
- T to type text into chat.
- Forward slash (/) to enter a command.
- Arrow keys emulate mouse movement.
- Enter emulates mouse click.

### Chat Commands

    /server HOST [PORT]

Switch to the specified server.

    /view CHUNKS

Change the render distance.

### Implementation Details

#### Rendering

The game is fully raytraced.

The whole world is sent to the GPU as a 3D texture, so you need a lot of video
memory to run it well. If it runs out of memory, try making the render distance
smaller.

Every chunk received is written back to the 3D texture as soon as possible. Only
the modified chunks are sent.

Text is rendered using a bitmap atlas. Each character is rendered onto two
triangles forming a 2D rectangle.

“Modern” OpenGL is used - no deprecated, fixed-function pipeline functions are
used. Vertex buffer objects are used for position, normal and texture
coordinates. Vertex and fragment shaders are used for rendering. Matrix
manipulation functions are in matrix.c for translation, rotation, perspective,
orthographic, etc. matrices.

Raytracing is implemented by drawing a quad covering the screen and doing raytracing
on the fragment shader (block_fragment.glsl). This renders the world, textures,
transparency, refraction and reflection (currently disabled). Rays are traced
from the camera by using a precise cube-stepping algorithm for the first quarter
of the render distance and an imprecise method for the rest (that skips a lot of
blocks and speeds up distant stuff quite a bit). Then the block at that position
has its texture retrieved and shown.

There is currently no lighting.

#### World

In game, the chunks store their blocks in a giant array. Light sources are stored
in a hash map, mapping (x, y, z) to light level.

Chunks are cubic, 32x32x32. This means also that there is no upper height limit,
however, since the world does not generate under y = 0, users are not allowed to
destroy blocks at y = 0 to avoid falling underneath the world.

#### Multiplayer

Multiplayer mode is implemented using plain-old sockets. A simple ASCII protocol
is used. Each message is made up of a two-byte length, a command code and zero
or more comma-separated arguments. The client requests chunks from the server
with a simple command: `C,p,q,r`. `C` means “Chunk” and (`p`, `q`, `r`) identifies
the chunk. Chunks are sent back with: `C[64-bit p][64-bit q][64-bit r]` followed
by the chunk blocks, one byte per block. Realtime block updates are sent to the
client in the format: `B,x,y,z,w`. Player positions are sent in the format:
`P,pid,x,y,z,rx,ry`. The pid is the player ID and the rx and ry values indicate
the player’s rotation in two different axes. The client interpolates player
positions from the past two position updates for smoother animation. The client
sends its position to the server at most every 0.1 seconds (less if not moving).

#### Collision Testing

Hit testing (what block the user is pointing at) is implemented by scanning a
ray from the player’s position outward, following their sight vector. This is
not a precise method, so the step rate can be made smaller to be more accurate.

Collision testing simply adjusts the player’s position to remain a certain
distance away from any adjacent blocks that are obstacles. (Clouds and plants
are not marked as obstacles, so you pass right through them.)

#### Dependencies

* GLEW is used for managing OpenGL extensions across platforms.
* GLFW is used for cross-platform window management.
* lodepng is used for loading PNG textures.
* tinycthread is used for cross-platform threading.

