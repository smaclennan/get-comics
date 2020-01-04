# What is it?

I read roughly 40 web comics daily, so I wrote get-comics to
conveniently download all the comics into one place. It uses a simple
json configuration file to define how to download the comics. The
sample one included is the one I use every day.

It broadly has two modes: it can either download the comics as images
or create a file of links to the comics. For day-to-day use I
generally just download the images. But if I am going away and may
have limited or sporadic internet access, I will run get-comics from
cron at home and just keep track of the links.

Here is the alias I use to download the comics under Linux:

    get-today='get-comics -cd ~/comics/today -i ~/comics/index'

## How it works

For all the active comics I read there are two stages. First, we
download the html page for the comic and scan the page using a
regular expression to find the image. Then we download the image or
just put the link in the links file.

## Why are there different versions?

I use get-comics every day, so it is a good vehicle to test and
experiment with http clients. As such, I have a raw C version of the
program. This is the one I use most often to try to keep up with
changes in how http is used.

The C version is single threaded with non-blocking I/O... just because
I can. It also supports persistent connections.

There is also a version using libcurl. It mimics the C version but
with less code. The curl version can be either single or
multi-threaded. This is the smarter way to write the code since
libcurl "just works" and if it doesn't there is an active community
to fix it.

There is a multi-threaded python (get-comics.py) version of
get-comics. This make even more sense since it needs no compiling and
is a single, <400 line, file. Note: the python version does need the
python requests package which you probably do not have. See the
comment at the top of the get-comics.py file. Unless you have a super
fast internet connection, the python script is just fine.

And lastly, there is a multi-threaded go version (get-comics.go) of
the program. This was just for me to experiment with go and is less
full featured than the other versions. But if you like go, go ahead and
use it!

## OS Agnostic

I have tried to make all the versions OS agnostic. While Linux is my
main platform, and therefore the most tested, the C and curl versions
should work fine under any Unix and Microsoft Windows. The python version
should work anywhere python is available.
