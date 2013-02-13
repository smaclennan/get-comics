package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"io/ioutil"
	"net/url"
	"os"
	"regexp"
	"strings"
	"strconv"
	"time"
)

const default_config = "/tmp/test.json"
//const default_config = "/usr/share/get-comics/comics.json"
var verbose = false


var comics_dir string
var gocomics_regexp string
var thread_limit = 4
var threads_set = false
var read_timeout = 500
var randomize = 0

var now = time.Now()
var wday = weekday2int()
var skipping = 0

type Comic struct {
	url string
	host string /* filled by parse_comic */
	regexp string
	regfname string /* filled by parse_comic */
	regmatch int
	outname string
	base_href string
	referer string
	redirect_ok int
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
				if u.Scheme != "http" && u.Scheme != "https" {
					fmt.Println("Skipping not http: ", comic.url)
					skipping += 1
					return
				}
				comic.host = u.Host
			case "days":
				if val[wday] == 'X' {
					fmt.Println("Skipping: ", comic.url) // SAM DBG
					skipping += 1
					return
				}
			case "regexp":
				comic.regexp = strftime(val)
				comic.regfname = "index-" + strconv.Itoa(id) + ".html"
			case "output":
				comic.outname = val
			case "href":
				comic.base_href = val
			case "referer":
				if val == "url" {
					comic.referer = comic.url
				} else {
					comic.referer = val
				}
			case "gocomic":
				comic.url = "http://www.gocomics.com/" + val + "/"
				comic.host = "www.gocomics.com"
				comic.regexp = gocomics_regexp
				comic.outname = val
			default:
				fmt.Println("Unexpected element ", k)
			}
		case float64:
			switch k {
			case "regmatch":
				comic.regmatch = int(val)
			case "redirect":
				comic.redirect_ok = int(val)
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

	if verbose { fmt.Println(f) }

	m := f.(map[string]interface{})

	for k, v := range m {
		switch vv := v.(type) {
		case string:
			switch k {
			case "directory":
				/* Do not override the command line option */
				if comics_dir == "" {
					comics_dir = vv
				}
			case "proxy":
				fmt.Println("Warning: proxy not supported.")
			case "gocomics-regexp":
				gocomics_regexp = vv
			default:
				fmt.Println("Unexpected element ", k)
			}
		case float64:
			switch k {
			case "threads":
				if !threads_set {
					thread_limit = int(vv)
				}
			case "timeout":
				read_timeout = int(vv)
			case "randomize":
				randomize = int(vv)
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

func main() {
	flag.Parse()
	if len(flag.Args()) > 0 {
		for i := range flag.Args() {
			read_config(flag.Arg(i))
		}
	} else {
		read_config(default_config)
	}
}

/*
 * Local Variables:
 * compile-command: "gccgo get-comics.go -o get-comics"
 * End:
 */
