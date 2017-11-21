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
    today = 0

    def __init__(self):
        self.url = None
        self.host = None
        self.regexp = None
        self.regmatch = 0
        self.outname = None
        self.base_href = None
        self.referer = None
        self.skip = False

    def add_days(self, days):
        if days[Comic.today] == 'X':
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
            new_comic.url = comic['url']
        elif each == 'days':
            new_comic.add_days(comic['days'])
        elif each == 'regexp':
            new_comic.regexp = comic['regexp']
        elif each == 'output':
            new_comic.output = comic['output']
        elif each == 'href':
            new_comic.href = comic['href']
        elif each == 'referer':
            new_comic.referer = comic['referer']
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
    Comic.today = date.today().weekday() + 1
    if Comic.today == 7: Comic.today = 0

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

def outfile (comic, text):
    try:
        fp = open(comics_dir + '/' + comic.outname, 'w')
        fp.write(text)
        fp.close()
    except Exception,e:
        print str(e.args[1]) + ": " + comic.outname

def stage2 (comic, match):
    r = requests.get(match.group(comic.regmatch))
    if r.status_code != 200:
        print "Error:" + comic.url + " " + str(r.status_code)
        return
    outfile(comic, r.content)

def comic_thread (comic):
    global sema

    r = requests.get(comic.url)
    if r.status_code == 200:
        if comic.regexp:
            m = re.search(comic.regexp, r.text)
            if m:
                stage2(comic, m)
            else:
                print comic.url + " did not match regexp"
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
parser.add_argument('-d', metavar='directory', help='comics directory')
args = parser.parse_args()

read_config("/tmp/comics.json")

# Setup comics_dir and make sure it exists
if args.d:
    comics_dir = args.d
elif comics_dir == None:
    comics_dir = os.getenv('HOME') + '/comics'
if os.path.isdir(comics_dir) == None:
    print "ERROR: " + comics_dir + " does not exist"
    sys.exit(1)

# We use a semaphore to limit the number of outstanding threads
sema = threading.Semaphore(nthreads)

for comic in comics:
    if comic.skip == False:
        sema.acquire()
        thread = threading.Thread(target=comic_thread, args=(comic, ))
        threads.append(thread)
        print "Starting " + thread.name + " " + comic.url # SAM DBG
        thread.start()

for thread in threads:
    if comic.skip == False:
        thread.join()
        print "Done " + thread.name # SAM DBG
