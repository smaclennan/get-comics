#!/usr/bin/python

import sys, os, re, json, threading, requests, argparse, time, random
from datetime import date

comment_re = re.compile(r"/\*(.*?)\*/")
multi_comment_re = re.compile(r"/\*(.*?)\*/", re.DOTALL)

comics_dir = None

comics = list()
nthreads = 10
threads = list()

class Comic:
    gocomics_regexp = None
    weekday = 0
    today = 0
    __id = 0

    def __init__(self):
        self.url = None
        self.host = None
        self.regexp = None
        self.regmatch = 0
        self.skip = False
        # Make sure every comic has an outname
        self.outname = "comic" + str(Comic.__id)
        Comic.__id = Comic.__id + 1

    def add_url(self, url):
        self.url = time.strftime(url, Comic.today)
        m = re.match(r"(https?://[^/]+)", url)
        if m:
            self.host = m.group(1)
        else:
            self.host = ""
            print "ERROR: Unable to isolate host: " + url

    def add_regexp(self, regexp):
        self.regexp = time.strftime(regexp, Comic.today)

    def add_days(self, days):
        if days[Comic.weekday] == 'X':
            self.skip = True

    def add_gocomic(self, comic):
        if self.gocomics_regexp == None:
            print "PROBLEMS: No gocomics_regexp"
            sys.exit(1)
        self.url = "http://www.gocomics.com/" + comic + "/"
        self.regexp = self.gocomics_regexp
        self.outname = comic

def strip_comments (data):
    # First remove all single line comments to solve nested comment
    # problems
    while True:
        m = re.search(comment_re, data)
        if m == None:
            break
        data = data[:m.start()] + data[m.end():]

    # Now remove the multiline comments
    while True:
        m = re.search(multi_comment_re, data)
        if m == None:
            break
        data = data[:m.start()] + data[m.end():]

    return data

def parse_comic (comic):
    new_comic = Comic()
    comics.append(new_comic)
    for each in comic:
        if each == 'url':
            new_comic.add_url(comic['url'])
        elif each == 'days':
            new_comic.add_days(comic['days'])
        elif each == 'regexp':
            new_comic.add_regexp(comic['regexp'])
        elif each == 'output':
            new_comic.outname = comic['output']
        elif each == 'referer':
            pass
        elif each == 'gocomic':
            new_comic.add_gocomic(comic['gocomic'])
        else:
            print("Unexpected key " + each)

def read_config (fname):
    global comics_dir

    try:
        fp = open(fname)
        data = fp.read()
        fp.close()
    except Exception,e:
        print(e.args[1] + ": " + fname)
        sys.exit(1)

    # Python json cannot handle comments
    data = strip_comments(data)

    data = json.loads(data)

    try: Comic.gocomics_regexp = data['gocomics-regexp']
    except: pass

    # Get the weekday and make Sunday 0
    Comic.today = time.localtime();
    Comic.weekday = date.today().weekday() + 1
    if Comic.weekday == 7: Comic.weekday = 0

    for each in data:
        if each == 'comics':
            for comic in data['comics']:
                if len(comic) > 0:
                    parse_comic(comic)
        elif each == 'threads':
            nthreads = data['threads']
        elif each == 'directory':
            comics_dir = data['directory']
        elif each != 'gocomics-regexp':
            print "WARNING: " + str(each)

imgtype = { 'GIF8':             '.gif',
            '\x89PNG':          '.png',
            '\xff\xd8\xff\xe0': '.jpg', # jfif
            '\xff\xd8\xff\xe1': '.jpg', # exif
            '\xff\xd8\xff\xee': '.jpg', # Adobe
            'II\x42\0':         '.tif', # little endian
            'MM\0\x42':         '.tif' # little endian
            }

file_exts = { '.gif', '.png', '.jpg', '.tif', '.xxx', '.html' }

# This is a very lazy checking heuristic since we expect the files to
# be one of the four formats and well formed. Yes, Close To Home
# actually used TIFF. TIFF is only tested on little endian machines.
def lazy_imgtype (content):
    # print ':'.join(x.encode('hex') for x in content[0:4])
    # print content[0:4]
    try: return imgtype[content[0:4]]
    except: return '.xxx'

def clean_directory (dir):
    global comics_dir

    for file in os.listdir(dir):
        if os.path.splitext(file)[1] in file_exts:
            os.remove(comics_dir + '/' + file)
        else:
            print 'Skipping ' + file

def outfile (comic, text, addext = True):
    if addext:
        ext = lazy_imgtype(text)
    else:
        ext = ""
    try:
        fp = open(comics_dir + '/' + comic.outname + ext, 'w')
        fp.write(text)
        fp.close()
    except Exception,e:
        print str(e.args[1]) + ": " + comic.outname

def stage2 (comic, url):
    if url.startswith("http"):
        pass
    elif url.startswith("//"):
        url = "http:" + url
    else:
        url = comic.host + "/" + url

    r = requests.get(url)
    if r.status_code != 200:
        print "Error:" + url + " " + str(r.status_code)
        return
    outfile(comic, r.content)

def comic_thread (comic):
    global sema

    r = requests.get(comic.url)
    if r.status_code == 200:
        if comic.regexp:
            m = re.search(comic.regexp, r.text)
            if m:
                stage2(comic, m.group(comic.regmatch))
            else:
                print "ERROR: " + comic.url + " did not match regexp"
                # Save the file for debugging
                comic.outname = comic.outname + ".html"
                outfile(comic, r.text.encode('ascii', 'ignore'), False)
        else:
            outfile(comic, r.content)
    else:
        print "Error:" + comic.url + " " + str(r.status_code)

    sema.release()

def comic_thread2 (comic): # Dummy version for testing
    global sema

    time.sleep(random.randint(1, 4))
    sema.release()

### Main

parser = argparse.ArgumentParser()
parser.add_argument('-c', action='store_true', help='clean comics directory')
parser.add_argument('-d', metavar='comics dir', help='comics directory')
parser.add_argument('-v', action='count', help='verbose')
parser.add_argument('config', nargs='?', help='config file', default="/usr/share/get-comics/comics.json")
args = parser.parse_args()

read_config(args.config)

# Setup comics_dir and make sure it exists
if args.d:
    comics_dir = args.d
elif comics_dir == None:
    comics_dir = os.getenv('HOME') + '/comics'
if not os.path.isdir(comics_dir):
    print "ERROR: " + comics_dir + " does not exist"
    sys.exit(1)

if args.c:
    clean_directory(comics_dir)

# We use a semaphore to limit the number of outstanding threads
sema = threading.Semaphore(nthreads)

for comic in comics:
    if comic.skip == False:
        sema.acquire()
        thread = threading.Thread(target=comic_thread, args=(comic, ))
        threads.append(thread)
        if args.v: print "Starting " + thread.name + " " + comic.url
        thread.start()

for thread in threads:
    if comic.skip == False:
        thread.join()
        if args.v: print "Done " + thread.name