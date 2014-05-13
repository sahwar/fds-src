#!/usr/bin/env python

import random
import pycurl
import re
import StringIO
import subprocess
from string import Template

class PushTemplate:

    def __init__(self):
        
        self.json_templ = Template('''
{
    "Queue-Workload": {
    	"Queue-Push": {
		"queue-name": "$t_name",
    		"value": $t_value
    	}
    }
}
''')

    def gen(self):
        value = random.randint(0, 1000)
        return self.json_templ.substitute(t_name = "test-queue", t_value = value)
        

class PopTemplate:
    
    def __init__(self):
        self.json_templ = Template('''
{
    "Queue-Workload": {
    	"Queue-Pop": {
    		"queue-name": "$t_name"
    	 }
    }
}
''')

    def gen(self):
        return self.json_templ.substitute(t_name = "test-queue")


class CreateTemplate:
    def __init__(self):

        self.json_templ = Template('''
{
    "Queue-Setup": {
        "Queue-Info": {
            "queue-name": "test-queue",
            "queue-size": $t_size
        }
    }
}
''')
    def gen(self):
        return self.json_templ.substitute(t_size = 1024)

def make_pycurl(url='http://localhost:8000/abc',
                verbose=0):
    '''
    Creates a pycurl object with correct settings for the probe
    '''
    c = pycurl.Curl()
    c.setopt(pycurl.URL, url)
    c.setopt(pycurl.POST, 1)
    c.setopt(pycurl.VERBOSE, verbose)
    
    return c

def do_queue_test():
    # First start the probe process
    probe = subprocess.Popen(['/home/brian/Documents/fds-src/source/Build/linux-x86_64.debug/tests/fds-probe-queue'],
                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    # Do pycurl creation
    pc = make_pycurl("http://localhost:8000/abc/")

    pc.setopt(pycurl.POSTFIELDS, CreateTemplate().gen())
    pc.perform()
    
    opts = [PushTemplate(), PopTemplate()]
    
    for i in range(10000):
        x = random.sample(opts, 1)
        x = x[0]
        pc.setopt(pycurl.POSTFIELDS, x.gen())
        pc.perform()

    stdout, stderr = probe.communicate()
    fh = StringIO.StringIO(stdout)
    validate(fh)


def validate(fh):
    
    in_re = re.compile(r'Pushing (\d{1,4}) to .*')
    out_re = re.compile(r'Popped value = (\d{1,4}) from .*')

    hist_queue = []
    active_queue = []
    act_idx = 0

    for line in fh:
        # If its a queue-in
        res = in_re.match(line)
        if res is not None:
            # Add it to both history queue and active queue
            print "Pushing", res.group(1)
            hist_queue.append(res.group(1))
            active_queue.append(res.group(1))
            continue

        # If it is not a queue-in check for queue-out
        res = out_re.match(line)
        if res is not None:
            print "Popping", res.group(1)
            # Remove it from active queue
            val = active_queue.pop(0)
            # Validate that it is the correct value
            assert val == hist_queue[act_idx], "Popped value not correct!"
            act_idx += 1
            continue            


if __name__ == "__main__":

    do_queue_test()
    print "Queue test: PASSED!"
