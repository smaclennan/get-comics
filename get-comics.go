package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"io/ioutil"
	"net/http"
	"net/url"
	"os"
	"regexp"
	"strings"
	"time"
)

const default_config = "/usr/share/get-comics/comics.json"

var comics_dir = flag.String("d", "", "Output directory for comics")
var thread_limit = flag.Int("t", 4, "Maximum number of concurrent gets")
var gocomics_regexp regexp.Regexp

var now = time.Now()
var wday = weekday2int()

// Counts
var total = 0
var got = 0
var skipped = 0

type Comic struct {
	// From the config file
	url string
	host string
	regexp regexp.Regexp
	regmatch int
	outname string
	base_href string
	referer string

	running, done bool
}

var comics []Comic

func weekday2int() int {
	switch now.Weekday() {
	case time.Sunday:    return 0
	case time.Monday:    return 1
	case time.Tuesday:   return 2
	case time.Wednesday: return 3
	case time.Thursday:  return 4
	case time.Friday:    return 5
	case time.Saturday:  return 6
	default:             return 0
	}
}

// This is not a complete implementation
func strftime(str string) string {
	if strings.Index(str, "%") != -1 {
		str = strings.Replace(str, "%Y", now.Format("2006"), -1)
		str = strings.Replace(str, "%m", now.Format("01"), -1)
		str = strings.Replace(str, "%d", now.Format("02"), -1)

		if strings.Index(str, "%") != -1 {
			panic("Bad time format: " + str)
		}
	}

	return str
}

func parse_comic(m map[string]interface{}, id int) {
	comic := make([]Comic, 1, 1)[0]

	for k, v := range m {
		switch val := v.(type) {
		case string:
			switch k {
			case "url":
				comic.url = strftime(val)

				u, err := url.Parse(comic.url)
				if err != nil {
					fmt.Println("Invalid url: ", comic.url)
					os.Exit(1)
				}
				comic.host = u.Scheme + "://" + u.Host
			case "days":
				if val[wday] == 'X' {
					skipped += 1
					return
				}
			case "regexp":
				val = strftime(val)
				comic.regexp = *regexp.MustCompile(val)
			case "output":
				comic.outname = val
			case "href":
				comic.base_href = val
				fmt.Println("Warning: We don't support href yet", comic.url)
				skipped += 1
				return
			case "referer":
				if val == "url" {
					comic.referer = comic.url
				} else {
					comic.referer = val
				}
			case "gocomic":
				comic.url = "http://www.gocomics.com/" + val + "/"
				comic.host = "http://www.gocomics.com"
				comic.regexp = gocomics_regexp
				comic.outname = val
			default:
				fmt.Println("Unexpected element ", k)
			}
		case float64:
			switch k {
			case "regmatch":
				comic.regmatch = int(val)
			case "redirect": /* redirects work silently */
			default:
				fmt.Println("Unexpected element ", k)
			}
		default:
			fmt.Println("Unkown element ", k)
		}
	}

	if comic.url == "" {
		fmt.Println("ERROR: comic with no url!")
		os.Exit(1)
	}

	comics = append(comics, comic)
	total += 1
}

func read_config(configfile string) {
	config, err := ioutil.ReadFile(configfile)
	if err != nil {
		fmt.Println(configfile, ": ", err)
		os.Exit(1)
	}

	// go json does not support comments... strip them
	re := regexp.MustCompile("/\\*.*\\*/")
	config = re.ReplaceAll(config, nil)

	var f interface{}
	err = json.Unmarshal(config, &f)
	if err != nil {
		fmt.Println("Unmarshal ", err)
		os.Exit(1)
	}

	m := f.(map[string]interface{})

	for k, v := range m {
		switch vv := v.(type) {
		case string:
			switch k {
			case "directory":
				/* Do not override the command line option */
				if *comics_dir == "" {
					*comics_dir = vv
				}
			case "proxy":
				fmt.Println("Warning: proxy not supported.")
			case "gocomics-regexp":
				gocomics_regexp = *regexp.MustCompile(vv)
			default:
				fmt.Println("Unexpected element ", k)
			}
		case float64:
			switch k {
			case "threads":
				*thread_limit = int(vv)
			case "timeout":
				fmt.Println("Warning: timeout not supported.")
			case "randomize":
				fmt.Println("Warning: randomize not supported.")
			default:
				fmt.Println("Unexpected element ", k)
			}
		case []interface{}:
			if k == "comics" {
				for id, u := range vv {
					comic := u.(map[string]interface{})
					if len(comic) > 0 {
						parse_comic(comic, id)
					}
				}
			} else {
				fmt.Println("Unexpected element ", k)
			}
		default:
			fmt.Println("Unkown element ", k)
		}
	}
}

/* This is a very lazy checking heuristic since we expect the files to
 * be one of the four formats and well formed. Yes, Close To Home
 * actually used TIFF. TIFF is only tested on little endian machine. */
func lazy_imgtype(hdr []byte) string {
	switch hdr[0] {
	case 'G':
		return ".gif"
	case 0xff:
		return ".jpg"
	case 0x89:
		return ".png"
	case 'M':
		return ".tif"
	case 'I':
		return ".tif"
	default:
		fmt.Println("Warning: Unknow file type: ", hdr)
		return ".xxx"
	}
}

func set_outname(comic Comic, hdr []byte) string {
	if comic.outname == "" { // Get file name from url
		ulen := len(comic.url)
		index := strings.LastIndex(comic.url, "/") + 1
		if index == 0 {
			comic.outname = comic.url
		} else if index >= ulen {
			comic.outname = "index.html"
		} else {
			comic.outname = comic.url[index:ulen]
		}
	}

	i := strings.LastIndex(comic.outname, ".")
	if i == -1 {
		comic.outname += lazy_imgtype(hdr)
	}

	return comic.outname
}

func gethttp(comic Comic, writeit bool) []byte {
	var resp *http.Response
	var err error

	if comic.referer != "" {
		client := &http.Client {}

		var req *http.Request
		req, err = http.NewRequest("GET", comic.url, nil)
		if err == nil {
			req.Header.Add("Referer", comic.referer)
			resp, err = client.Do(req)
		}
	} else {
		resp, err = http.Get(comic.url)
	}
	if err != nil {
		fmt.Println(comic.url, ": ", err)
		return nil
	}

	if resp.Status != "200 OK" {
		fmt.Println(comic.url, ": ", resp.Status)
		return nil
	}

	body, err := ioutil.ReadAll(resp.Body)
	resp.Body.Close()
	if err != nil {
		fmt.Println(comic.url, ": ", err)
		return nil
	}

	if !writeit { return body }

	outname := set_outname(comic, body[0:4])
	err = ioutil.WriteFile(outname, body, 0644)
	if err != nil {
		fmt.Println(outname, ": ", err)
	} else {
		got += 1
	}

	return nil
}

func find_match(comic Comic, body []byte) string {
	if comic.regmatch == 0 {
		matchb := comic.regexp.Find(body)
		if matchb == nil {
			fmt.Println(comic.url, ": No match.")
			return ""
		}

		return string(matchb)
	} else {
		matchb := comic.regexp.FindSubmatch(body)
		if matchb == nil {
			fmt.Println(comic.url, ": No match.")
			return ""
		}
		if len(matchb) - 1 < comic.regmatch {
			fmt.Println(comic.url, ": No match for ", comic.regmatch)
			return ""
		}
		return string(matchb[comic.regmatch])
	}
}

func set_done(comic *Comic) {
	comic.done = true
	comic.running = false
}

func get_comic(comic *Comic) {
	comic.running = true
	defer set_done(comic)

	if comic.regexp.String() != "" {
		body := gethttp(*comic, false)

		match := find_match(*comic, body)
		if match == "" { return }

		if strings.HasPrefix(match, "http") {
			comic.url = match
		} else if match[0] == '/' {
			comic.url = comic.host + match
		} else {
			comic.url = comic.host + "/" + match
		}
	}

	gethttp(*comic, true)
}

func main() {
	flag.Parse()
	if len(flag.Args()) > 0 {
		for i := range flag.Args() {
			read_config(flag.Arg(i))
		}
	} else {
		read_config(default_config)
	}

	if *comics_dir != "" { os.Chdir(*comics_dir) }

	/* Braindead multithreaded */
	for {
		running := 0
		alldone := 0
		for i := range comics {
			if comics[i].done {
				alldone += 1
			} else if comics[i].running {
				running += 1
			}
		}

		if alldone == total { break }

		for i := range comics {
			if !comics[i].done && !comics[i].running && running < *thread_limit {
				go get_comic(&comics[i])
				running += 1
			}
		}

		fmt.Printf("Done %d Running %d Total %d\n", alldone, running, total)

		time.Sleep(500 * time.Millisecond)
	}


	if skipped > 0 {
		fmt.Printf("Got %d of %d (%d skipped)\n", got, total, skipped)
	} else {
		fmt.Printf("Got %d of %d\n", got, total)
	}
}

/*
 * Local Variables:
 * compile-command: "gccgo get-comics.go -o get-comics"
 * End:
 */
