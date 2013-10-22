#!/usr/bin/env python

import urllib2
import sys
import getopt
import re
import os

# re patterns for the following line: 
#     TOTALCOV -- '../../MConfig.c': Lines(1020) - executed:1.37%
#
line_prog = re.compile(".*TOTALCOV -- (.*): Lines\(([0-9]+)\) - executed:(.+)%.*")
count_pat = re.compile(r'.*[fF]ailures?:\s*(\d+),\s*[eE]rrors?: (\d+)')
loc_pat = re.compile(r"Entering directory `(.*)'$")

# Given file name, open and parse it, expecting output generated by 'gcov' coverage tool
# argument:  file path to gcov output file
# argument:  Diction to store results from the file
def process_file(file, report):
    root = ''
    if (file != None):
        root = os.path.dirname(os.path.abspath(file))
        try:
            input = open(file, 'r', 1)
        except IOError as (errno, strerror):
            print "'{0}' I/O error({1}): {2}".format(file,errno, strerror)
            return
        except:
            print "Unexpected error:", sys.exc_info()[0]
            return
    else:
        input = sys.stdin

    # read each line looking for:
    #   1) File '<stuff'
    #   2) Lines executed ....
    #
    # Remember the file name until we encounter its corresponding 'Lines executed' line
    # When we get both, add to the Dictionary and reset the file name
    errors_written = False
    filename = None
    for line in input.readlines():
        result = line_prog.search(line)
        if result != None:
            report[result.group(1)] = [result.group(3), result.group(2)]
        else:
            m = loc_pat.search(line)
            if m:
                filename = m.group(1)
                if root:
                    filename = filename[len(root) + 1:]
            else:
                m = count_pat.search(line)
                if m:
                    if int(m.group(1)) or int(m.group(2)):
                        if not errors_written:
                            sys.stderr.write('\nSOME TESTS DID NOT PASS.\n\n')
                            errors_written = True
                        sys.stderr.write('%s\n  %s\n' % (filename, line.rstrip()))
    if errors_written:
        sys.stderr.write('\n')

    input.close()

# Process and report the dictionary in 'report' argument
def report_file(report):

    if (len(report) == 0):
        return

    total_lines = 0
    actual_lines = 0.0
    list = []

    # extract the dict keys, and generate a list that can be sorted, etc
    keys = report.keys()
    for key in keys:
        value = report[key]
        list.append([key,value[0],value[1]])

    # sort the list
    list.sort()

    # Print info for each reported file and then give a summary total for all the files mentioned
    # item[0] is the name, item[1] is percentage, item[2] is line count
    for item in list:
        print "%30s  %10s%%  of %7s" % tuple(item)
        actual_lines += (float(item[1]) / 100) * float(item[2])
        total_lines += int(item[2])

    print "-----------------------------------------------------------"
    print "Files accounted for:" + str(len(list))
    percent_touched = (actual_lines / total_lines) * 100
    print "-----------------------------------------------------------"
    print "Lines executed: %6s of %6s statements: %.02f%% COVERAGE" % (int(actual_lines), total_lines, percent_touched)

    # Record this on Ben's website
    urlstr='http://moab-walldisplay/metrics/record.php?metric=10&date=now&value=';
    urlstr += str(percent_touched)
    urllib2.urlopen(urlstr)

    #Output this information to a comma separated values file
    csv = open('coverages.csv', 'w')

    csv.write('File name,Percentage Covered,Total Lines,Lines Covered\n')

    for item in list:
       csv.write(item[0] + ',' + item[1] + ',' + item[2] + ',')
       csv.write("%d" % (int((float(item[1]) / 100) * float(item[2]))))
       csv.write('\n')

    csv.close()


# program file1 [file2 ...]
#    for each argument, use that argument as a filename to be parsed and output
#    expected input is gcov output data where a File name and Lines executed is generated
def main():

    # No args, then no data file
    if len(sys.argv) < 2:
        report = {}
        process_file(None, report)
        report_file(report)
    else:
        files = sys.argv[1:]    # cut and drop the program name from the list

        # Process/parse each file and report the data
        for file in files:
            report = {}
            process_file(file, report)
            report_file(report)

if __name__ == "__main__":
    main()
