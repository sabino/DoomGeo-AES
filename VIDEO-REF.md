# Video Reference

Source URL: https://www.youtube.com/watch?v=4f1-7c6WX10

## Transcript

0:00 It seems like a common thing that every
0:02 piece of tech out there has a port of
0:04 Doom. This is because the code is
0:06 elegant, fast, modular, and runs on just
0:09 about everything. Even systems that came
0:12 before Doom have ports, and some ports
0:15 have had help thanks to custom hardware
0:17 like the original Doom on the SNES. Doom
0:20 is written in very clean and elegant CC
0:22 code. It's not difficult to learn and
0:24 follow along. It doesn't use any modern
0:27 language updates, nor does it rely on
0:29 anything that's depreciated, and it
0:31 compiles up rather nicely. It's also
0:34 Indianfriendly, which is very important
0:36 for portability. So, I think it's fair
0:38 to say that it's a bit of a surprise
0:40 that the Neo Geo has never seen a port
0:42 of Doom. After all, the Neo Geo shares
0:45 the same microprocessor CPU, the
0:48 Motorola 68000, as the Sega Genesis and
0:51 the Commodore Omega, both of which have
0:53 seen aftermarket versions of Doom
0:55 running on their respective hardware.
0:57 But on the Neo Geo, it's nowhere to be
0:59 found.
1:01 Many developers who've worked on the Neo
1:03 Geo have often said that Doom is near
1:06 impossible to run. But the question now
1:09 is why? Why would the Neo Geo be
1:11 impossible to run a port of Doom? After
1:14 all, it has a video display, a CPU, RAM,
1:17 and all the things to run a video game.
1:20 So, let's go ahead and dive in as to the
1:22 reasons why. Now, the Neo Geo runs a
1:25 Motorola 68,000 at 12 MHz. It has 64
1:29 kilob of RAM, 84 kilob of VRAM, and 2
1:33 kilob of sound memory. Its graphics
1:36 display runs at 320x224
1:39 resolution with a total of 340 oncreen
1:43 colors out of a pallet of 65,000. This
1:46 alone sounds like it would be an uphill
1:49 challenge for Doom, but we've seen Doom
1:51 running on all matters of hardware. So,
1:53 let's move on. The problem is that the
1:55 Neo Geo is a 2D spritebased hardware
1:58 beast, and sprites is exactly what the
2:01 Neo Geo uses to render its display. The
2:04 system draws its sprites in vertical
2:06 strips of tiles which are 16x6 pixels in
2:09 size with a limitation of 96 sprites per
2:12 scan line and it's possible to have
2:14 sprites as large as 16x 512 pixels. Like
2:19 the AmIgga, the Neo Geo has no bit map
2:22 graphics mode. That is to say, there is
2:24 no direct way to read and write pixels
2:26 to and from display memory. But we've
2:29 already seen Doom running on the Omega.
2:31 So how is this possible? Now on the
2:33 AmIgga, graphics could be drawn on
2:35 screen into chip memory by writing
2:37 values to the Omega's bit planes and a
2:40 conversion known as chunky topler is
2:42 used in Doom ports to the AmIGGA to
2:45 convert the bit map data that Doom
2:47 outputs to its frame buffer into a
2:49 suitable format that the Omega can
2:51 display. This process is quite slow and
2:54 it's one of the reasons that you need a
2:56 pretty fast 030 or higher accelerated
2:58 Omega to run Doom on. This alone puts
3:01 the Neo Geo at a serious disadvantage
3:03 because the hardware is fixed. It's not
3:05 feasible just to drop a more powerful
3:07 processor on the motherboard. But the
3:10 strength of the Neo Geo is 2D. There is
3:13 no concept of 3D line drawing or vector
3:16 graphics at all. Even before games like
3:19 Doom, the AmIgga had games such as Star
3:21 Glider 2 by Argonaut, many flight
3:24 simulators, and many 3D vector-based
3:26 games. The Omega's Blitter hardware can
3:29 draw lines and fill them relatively
3:30 quickly. In comparison, all the Neo Geo
3:34 has is 2D sprites. Now, a few Neo Geo
3:37 games tried to simulate real-time 3D,
3:40 such as Viewpoint and the Super Spy, but
3:42 in reality, these were all precomputed
3:45 sprite data stored in the Neo Geo's
3:47 cartridge. The Super Spy simulates a
3:50 firsterson environment by using a
3:52 feature of the hardware known as sprite
3:55 shrinking. shrinking, which is also
3:57 known as scaling, reduction, and
3:59 zooming, is a hardware feature which
4:02 allows sprites to be scaled down with
4:04 pixel perfect accuracy in both
4:06 dimensions. In fact, many games use this
4:08 feature, and it's a key visual of the
4:11 Neo Geo that gives it its own unique
4:13 style and charm. Fighting games would
4:16 often zoom in and out in sync with the
4:18 gameplay to extend that feeling of
4:20 immersion, and it was a pretty cool
4:22 effect. Now, so far we've said things
4:25 that really wouldn't stop a Doom port
4:27 from running on the Neo Geo. There must
4:29 be a way to work around this. But I
4:31 think the biggest challenge overall is
4:33 the fact that the 68,000 CPU has no
4:36 direct access to the graphics data on
4:39 any Neo Geo cartridge. And this is an
4:42 architectural fact that many people
4:45 overlooked. The CROM, which contains the
4:48 sprite graphics, is wired directly to
4:50 the LSPC video chip, but it's not on the
4:54 68000's address bus. This means that the
4:57 CPU cannot sample any data directly from
5:00 the CROM. It can't sample any texture
5:03 data. It can't read a pixel. It can't do
5:05 anything with the graphics data. The
5:08 only thing it can do is write references
5:10 such as tile numbers, positions, and
5:12 shrink values into VRAMm to let the
5:15 video chip fetch the actual pixels from
5:18 CROM by itself. And this is a
5:20 significantly different architecture
5:23 than a frame buffer machine or even on
5:25 the AmIgga with its bit planes where it
5:27 can read and write pixels freely. So
5:30 even if we consider a software renderer,
5:32 the Neo Geo simply cannot do it. it
5:34 can't read source graphics and there's
5:36 no frame buffer to write the results
5:37 into. So now it was time for a
5:40 challenge. Okay, let's assume that Doom
5:43 is off the table. Or maybe we need to
5:45 start simpler. Can we draw or render a
5:48 simple raycaster that something like
5:50 Wolfenstein 3D uses on the Neo Geo?
5:53 Would that be possible? We did say that
5:56 the Neo Geo draws everything using
5:58 sprites and the sprite is made up of
6:01 little images which are 16x6 pixel tiles
6:04 that are stacked into a vertical strip.
6:06 So the Neo Geo's approach is to maintain
6:08 a list of sprites and their positions
6:10 and properties in a special memory
6:13 region. And this hardware renders this
6:15 list every single frame. We also said
6:17 that the Neo Geo can shrink any sprite
6:20 independently in X and Y for free. So,
6:23 in theory, it would be possible to build
6:25 a raycaster where each wall slice is a
6:28 shrunk sprite. A raycaster screen is
6:31 built from vertical strips. For each
6:33 column on the screen, you can cast a ray
6:36 into the world, find where the wall
6:38 hits, measure the distance, and draw a
6:40 vertical line whose height depends on
6:42 distance. And with enough granularity,
6:44 the brain registers this as a 3D scene.
6:48 So, I did end up building a simple
6:49 raycaster on the Neo Geo. And the way it
6:52 works is that we have 80 sprites side by
6:54 side, each four pixels wide, sitting at
6:56 a fixed X position across the screen.
6:59 With every frame, we leave the X value
7:01 alone. And what we're doing is we're
7:03 altering the vertical Y shrink position.
7:05 The walls appear to move and change with
7:08 you, but we're not drawing anything. All
7:10 we're doing is adjusting 80 shrink
7:12 values. Now, with any raycaster, we have
7:15 a 2D grid of one and zero values. one
7:19 designating a wall and zero designating
7:21 as empty. The player has two values, a
7:24 position and a facing direction. For
7:26 each of the 80 screen columns, we simply
7:29 ask, if I shoot a ray from the player
7:32 out to the world in the direction that a
7:34 column represents, what wall cell does
7:36 it hit first and how far away is it? And
7:39 this is what the rendering function
7:40 does. It ray marches 80 times per frame
7:43 and fills three arrays. the shrink
7:45 values for each column, the Y position
7:47 for each column, and the color pallet
7:49 value for each column.
7:52 So, at this point, we have everything in
7:54 an array of values. But there's still
7:56 nothing rendering to the screen. Now,
7:58 the next thing that we need to do is
8:00 call our blit function, and this is
8:01 where we write our shrink values, our X
8:04 and Y positions, and sprite values to
8:07 the SCB or the sprite control block.
8:10 Now, for visibility, I also added a 2D
8:13 mini map on top, and I used the Neo
8:15 Geo's fix layer to do this. Now, the
8:18 fixed layer is meant for text and HUD
8:20 elements such as score and lives, and it
8:22 always draws on top of the sprites. It's
8:24 much less flexible, but it's perfect for
8:27 a 2D mini map. And this is the final
8:30 result. It's not too bad. Every single
8:32 frame, we read the joystick and move the
8:35 player. And for each of those 80 screen
8:37 columns, we march array through the grid
8:39 map until it hits a wall and then
8:41 measure the distance that turns into a
8:43 height and color. Then we stream those
8:45 80 height and positions and colors into
8:48 the sprite control block where each
8:49 column is a single textured sprite that
8:52 shrinks to the right height. We leverage
8:54 the Neo Geo scaling to do the actual
8:57 work for us. There's no frame buffer, no
8:59 pixel drawing, and certainly no floating
9:02 point. Now, a few things to note here.
9:04 This is far from optimized code. It's
9:06 something that I put together very, very
9:08 quickly and it runs probably at about 8
9:11 frames pers. We could definitely get
9:13 performance to be a lot better. But as
9:15 it stands, we're running this under
9:16 emulation as well. I have not tested
9:18 this on real hardware, though I don't
9:20 think it would be any different. And for
9:23 those who are interested, I've left the
9:24 source code in the description below.
9:26 Feel free to check it out as you need
9:27 to.
9:29 So with this simple proof of concept, I
9:31 do think that it is possible to build a
9:34 Wolfenstein style engine to run on the
9:37 Neo Geo one that uses full height
9:40 vertical strips. But I do agree that
9:42 Doom is probably not feasible on the Neo
9:45 Geo. Walls are no longer uniform height
9:48 strips. In Wolfenstein, every wall is
9:51 the same height and aligned to a grid.
9:54 So, a column is always one texture strip
9:56 top to bottom, which is scaled. In Doom,
9:59 there are things like sectors that have
10:01 arbitrary floors and ceiling heights.
10:03 You get things like ledges, staircases,
10:06 raised platforms, elevators, tunnels,
10:09 and windows that you can look through
10:10 into rooms beyond. So, we're no longer
10:13 talking about scaled strips. We would
10:15 need several independently positioned,
10:17 independently scaled sprites per column,
10:19 and they would have to be clipped
10:21 against each other. There's also the
10:23 concept of diagonal walls. In
10:25 Wolfenstein, walls are grid aligned.
10:28 That is, every wall faces exactly north,
10:30 south, east, and west. In Doom, walls
10:33 sit at any angle, and a single screen
10:35 column crossing a diagonal wall sees the
10:37 wall surface at a continually varying
10:39 distance down the column. So on the Neo
10:42 Geo, it would be very difficult, if not
10:44 impossible, to apply a per pixel varying
10:46 scale and perspective map of one sprite.
10:49 Diagonal walls need exactly that. So
10:52 even a single diagonal wall would be
10:54 difficult to map as a hardware scaled
10:56 strip. Other things such as floor and
10:58 ceilings are also a big problem. In the
11:01 raycaster, floors and ceilings are flat
11:04 colored regions. In Doom, floors and
11:06 ceilings are textured and perspective
11:08 mapped. Doom also had sectorbased
11:11 lighting per pixel via color maps. In
11:13 our raycast caster, we can do things
11:15 like banded pallet shading per column,
11:17 which works well. But with Doom's
11:19 lighting, which varies within a surface
11:22 and across floor pixels, the Neo Geo
11:25 must swap pixels per sprite, but it
11:27 cannot remap per pixel across a textured
11:29 plane. And this is before we're talking
11:32 about sprites, enemy monsters, weapons,
11:35 all the other stuff that makes Doom what
11:37 it is. and assuming you found a way to
11:39 solve for all this having all this
11:41 running on a 12 MHz CPU. Now, I'm smart
11:44 enough to tell you that I don't want to
11:46 say it's impossible because as soon as
11:48 you say something is impossible, the
11:50 gauntlet has been thrown down. But I do
11:52 think the only way that Doom can run on
11:55 the Neo Geo is with some extra hardware.
11:58 But that of course is just my opinion
12:00 with hacking a raycasting program that
12:02 runs on a Neo Geo with unoptimized code.
12:05 So, we're going to leave it here for
12:06 today's episode. I do hope you enjoyed
12:09 this look at the reasons why Doom seems
12:11 like an impossibility on the Neo Geo.
12:14 Now, I say seems like a impossibility
12:17 because later this year sees the release
12:19 of the Neo Geo AES Plus, which will
12:22 reinvigorate the homebrew community all
12:24 over again. And maybe someone out there
12:27 who has the right skills could
12:30 potentially bring us doom to the Neo
12:32 Geo. I think there is some possibility
12:35 that it could come. But for now, we are
12:37 going to leave it here for today's
12:39 episode. I do hope you enjoyed it. Thank
12:40 you so much for watching. And if you
12:42 liked it, leave me a thumbs up. And we
12:44 will catch you guys in the next episode.
12:46 Bye for now.
